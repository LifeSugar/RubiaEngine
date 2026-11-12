#include "resources/ResourcePreparationService.hpp"
#include "resources/ResourcePreparationSession.hpp"
#include "resources/RenderAssetStore.hpp"
#include "resources/TextureUploadBuilder.hpp"
#include <chrono>
#include <iostream>
#include <map>
#include <thread>

namespace
{
using namespace rubia;
void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
struct TestBuffer final : rhi::BufferResource
{
    rhi::BufferDesc desc;
    rhi::DeviceId deviceId() const noexcept override { return 1; }
    const rhi::BufferDesc &description() const noexcept override { return desc; }
    uint32_t memoryProperties() const noexcept override { return desc.requiredMemory; }
};
struct TestTexture final : rhi::TextureResource
{
    rhi::TextureDesc desc;
    rhi::DeviceId deviceId() const noexcept override { return 1; }
    const rhi::TextureDesc &description() const noexcept override { return desc; }
};
struct TestView final : rhi::TextureViewResource
{
    rhi::TextureViewDesc desc;
    rhi::TextureRef owner;
    rhi::DeviceId deviceId() const noexcept override { return 1; }
    const rhi::TextureViewDesc &description() const noexcept override { return desc; }
    const rhi::TextureRef &texture() const noexcept override { return owner; }
};
struct TestSampler final : rhi::SamplerResource
{
    rhi::SamplerDesc desc;
    rhi::DeviceId deviceId() const noexcept override { return 1; }
    const rhi::SamplerDesc &description() const noexcept override { return desc; }
};
struct TestLayout final : rhi::BindingLayoutResource
{
    rhi::BindingLayoutDesc desc;
    rhi::DeviceId deviceId() const noexcept override { return 1; }
    const rhi::BindingLayoutDesc &description() const noexcept override { return desc; }
};
struct TestSet final : rhi::BindingSetResource
{
    rhi::BindingSetDesc desc;
    rhi::DeviceId deviceId() const noexcept override { return 1; }
    const rhi::BindingSetDesc &description() const noexcept override { return desc; }
};
class TestDevice final : public rhi::Device
{
  public:
    bool failBindings = false;
    rhi::DeviceId id() const noexcept override { return 1; }
    rhi::DeviceLimits limits() const noexcept override { return {8, 128}; }
    rhi::BufferRef createBuffer(const rhi::BufferDesc &desc) const override
    {
        auto value = std::make_shared<TestBuffer>();
        value->desc = desc;
        return value;
    }
    void writeBuffer(const rhi::BufferRef &, const void *, uint64_t, uint64_t) const override {}
    rhi::TextureRef createTexture(const rhi::TextureDesc &desc) const override
    {
        auto value = std::make_shared<TestTexture>();
        value->desc = desc;
        return value;
    }
    rhi::TextureViewRef createTextureView(rhi::TextureRef owner,
                                          const rhi::TextureViewDesc &desc) const override
    {
        auto value = std::make_shared<TestView>();
        value->desc = desc;
        value->owner = std::move(owner);
        return value;
    }
    rhi::SamplerRef createSampler(const rhi::SamplerDesc &desc) const override
    {
        auto value = std::make_shared<TestSampler>();
        value->desc = desc;
        return value;
    }
    rhi::ShaderRef createShader(const rhi::ShaderCode &) const override
    {
        throw std::logic_error("this test does not create shaders");
    }
    rhi::BindingLayoutRef createBindingLayout(const rhi::BindingLayoutDesc &desc) const override
    {
        auto value = std::make_shared<TestLayout>();
        value->desc = desc;
        return value;
    }
    rhi::BindingSetRef createBindingSet(const rhi::BindingSetDesc &desc) const override
    {
        if (failBindings)
            throw rhi::ResourceError(rhi::ResourceErrorCode::DeviceFailure,
                                     "injected binding failure");
        auto value = std::make_shared<TestSet>();
        value->desc = desc;
        return value;
    }
};
/// Controlled completion, not a copy engine. Cancelled submitted records keep
/// their leases until complete() even after the caller releases its ticket.
class ControlledUploads final : public rhi::UploadService
{
  public:
    struct Record
    {
        rhi::UploadRequest request;
        rhi::UploadStatus status;
    };
    rhi::UploadTicket last;
    rhi::UploadEnqueueResult tryEnqueue(rhi::UploadRequest &request) override
    {
        auto record = std::make_shared<Record>();
        for (const auto &operation : request.operations)
            std::visit([&](const auto &op) { record->status.totalBytes += op.source.size; },
                       operation);
        record->status.state = rhi::UploadState::Uploading;
        record->status.submittedBytes = record->status.totalBytes;
        last = {++next_};
        records_.emplace(last.value, record);
        submitted_.emplace(last.value, record);
        record->request = std::move(request);
        return {rhi::UploadEnqueueCode::Accepted, last, {}};
    }
    rhi::UploadStatus query(rhi::UploadTicket ticket) const override
    {
        return records_.at(ticket.value)->status;
    }
    void cancel(rhi::UploadTicket ticket) override
    {
        auto &state = records_.at(ticket.value)->status.state;
        if (!rhi::uploadFinished(state))
            state = rhi::UploadState::Cancelled;
    }
    void releaseTicket(rhi::UploadTicket ticket) override
    {
        require(rhi::uploadFinished(query(ticket).state), "test released unfinished transfer");
        records_.erase(ticket.value);
    }
    void complete(rhi::UploadTicket ticket, bool fail = false)
    {
        auto record = submitted_.at(ticket.value);
        if (record->status.state != rhi::UploadState::Cancelled)
        {
            record->status.state = fail ? rhi::UploadState::Failed : rhi::UploadState::Completed;
            record->status.error = fail ? "injected transfer failure" : "";
            if (!fail)
                record->status.completedBytes = record->status.totalBytes;
        }
        record->request.operations.clear();
        submitted_.erase(ticket.value);
    }
    void tick(const rhi::UploadBudget &) override {}
    void drain() override
    {
        while (!submitted_.empty())
            complete({submitted_.begin()->first});
    }
    void shutdown() noexcept override
    {
        submitted_.clear();
        records_.clear();
    }
    uint64_t stagedBytes() const noexcept override { return 0; }

  private:
    uint64_t next_ = 0;
    std::map<uint64_t, std::shared_ptr<Record>> records_, submitted_;
};
asset::TextureAsset::CreateInfo textureInfo(std::byte value)
{
    asset::TextureAsset::CreateInfo info;
    info.width = info.height = 2;
    info.format = asset::TextureFormat::RGBA8UNorm;
    info.payload.assign(16, value);
    return info;
}
void waitForTransfer(render::ResourcePreparationService &preparations,
                     render::ResourcePreparationTicket ticket)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (preparations.status(ticket).state != render::ResourcePreparationState::Uploading)
    {
        preparations.advance();
        require(!render::resourcePreparationFinished(preparations.status(ticket).state),
                "preparation failed before transfer admission");
        require(std::chrono::steady_clock::now() < deadline,
                "resource creation worker did not finish");
        std::this_thread::yield();
    }
}
void testCancellationAndTransferFailure()
{
    TestDevice device;
    ControlledUploads uploads;
    render::RenderAssetStore cache;
    render::RetirementBatch retired;
    render::ResourcePreparationService preparations(device, uploads, retired);
    asset::AssetManager assets;
    const auto handle = assets.createTexture(textureInfo(std::byte{1}));
    auto request = preparations.prepareTexture(cache, assets.snapshot(handle));
    require(request.accepted(), "initial preparation rejected");
    waitForTransfer(preparations, request.ticket);
    uploads.complete(uploads.last);
    preparations.advance();
    require(preparations.status(request.ticket).state == render::ResourcePreparationState::Ready,
            "completed resource was not published");
    preparations.release(request.ticket);
    const auto original = cache.texture(handle).view();

    static_cast<void>(
        assets.replaceTexture(handle, asset::TextureAsset(textureInfo(std::byte{2}))));
    const auto failedVersion = assets.snapshot(handle);
    request = preparations.prepareTexture(cache, failedVersion);
    waitForTransfer(preparations, request.ticket);
    uploads.complete(uploads.last, true);
    preparations.advance();
    require(preparations.status(request.ticket).state == render::ResourcePreparationState::Failed &&
                cache.texture(handle).view() == original && retired.empty(),
            "failed GPU transfer replaced or retired the resident texture");
    preparations.release(request.ticket);

    auto cancellationSource = std::make_shared<asset::TextureAsset>(textureInfo(std::byte{3}));
    std::weak_ptr<const asset::TextureAsset> sourceLifetime = cancellationSource;
    const asset::TextureAssetHandle cancelledHandle{1, 1};
    request = preparations.prepareTexture(
        cache, {assets.domain(), {cancelledHandle, 1}, cancellationSource});
    cancellationSource.reset();
    waitForTransfer(preparations, request.ticket);
    const auto pendingTransfer = uploads.last;
    preparations.cancel(request.ticket);
    preparations.release(request.ticket);
    require(!sourceLifetime.expired() && !cache.tryTexture(cancelledHandle),
            "cancel destroyed a submitted source or published an incomplete resource");
    uploads.complete(pendingTransfer);
    preparations.advance();
    require(sourceLifetime.expired() && !cache.tryTexture(cancelledHandle),
            "cancelled completion leaked source bytes or published abandoned work");

    request = preparations.prepareTexture(cache, failedVersion);
    require(request.accepted(), "failed version could not retry");
    waitForTransfer(preparations, request.ticket);
    uploads.complete(uploads.last);
    preparations.advance();
    require(preparations.status(request.ticket).state == render::ResourcePreparationState::Ready &&
                cache.texture(handle).view() != original && retired.size() == 1,
            "retry did not atomically replace and retire the old texture");
    preparations.release(request.ticket);
}
class TestPreparationHost final : public render::ResourcePreparationHost
{
  public:
    explicit TestPreparationHost(render::ResourcePreparationService &service) : service_(service) {}
    bool available = true;
    bool canPrepareResources() const noexcept override { return available; }
    render::ResourcePreparationService &resourcePreparationService() override { return service_; }
    void advanceResourcePreparation() override { service_.advance(); }
    void createPreparedSceneResources(std::shared_ptr<const render::PreparedMaterialTemplate>,
                                      std::shared_ptr<const render::PreparedShaderProgram>,
                                      uint32_t) override
    {
        throw std::logic_error("no scene in session test");
    }
    render::PipelinePreparationResult
    prewarmScenePipelines(const render::RenderAssetStore &,
                          const std::vector<asset::MaterialAssetHandle> &) override
    {
        return {};
    }
    render::PipelinePreparationStatus
    pipelinePreparationStatus(render::PipelinePreparationTicket) const override
    {
        throw std::out_of_range("no pipeline in session test");
    }
    void cancelPipelinePreparation(render::PipelinePreparationTicket) override {}
    void releasePipelinePreparation(render::PipelinePreparationTicket) override {}
    void releasePreparedSceneResources() noexcept override {}

  private:
    render::ResourcePreparationService &service_;
};
void testSessionOwnershipAndSharedWork()
{
    TestDevice device;
    ControlledUploads uploads;
    render::RenderAssetStore cache;
    render::RetirementBatch retired;
    render::ResourcePreparationService preparations(device, uploads, retired);
    TestPreparationHost host(preparations);
    render::ResourcePreparationSession first(host, cache), second(host, cache);
    asset::AssetManager assets;
    const auto handle = assets.createTexture(textureInfo(std::byte{1}));
    host.available = false;
    require(!first.prepare(assets.snapshot(handle)).accepted() && preparations.empty(),
            "closed renderer admission created preparation work");
    host.available = true;
    const auto a = first.prepare(assets.snapshot(handle));
    const auto b = second.prepare(assets.snapshot(handle));
    require(a.accepted() && b.accepted() && a.ticket.value != b.ticket.value &&
                b.disposition == render::ResourcePreparationDisposition::Shared,
            "sessions duplicated preparation work or shared caller tickets");
    bool foreignTicketRejected = false;
    try
    {
        first.cancel(b.ticket);
    }
    catch (const std::out_of_range &)
    {
        foreignTicketRejected = true;
    }
    require(foreignTicketRejected, "one session cancelled another session's ticket");
    first.cancelAll();
    waitForTransfer(preparations, b.ticket);
    uploads.complete(uploads.last);
    second.advance();
    require(second.status(b.ticket).state == render::ResourcePreparationState::Ready &&
                cache.tryTexture(handle),
            "closing one session abandoned another subscriber's shared resource");
    second.release(b.ticket);
    require(preparations.empty(), "session release leaked caller records");
}
void testImmutableMaterialTextureBindings()
{
    TestDevice device;
    asset::MaterialTemplateAsset::CompiledCreateInfo info;
    info.program = {0, 1};
    info.parameterDataSize = 16;
    info.programInterfaceSignature = 1;
    info.textureSlots.push_back({"albedo", 0, true, {1, 1}, {1, 2}});
    asset::MaterialTemplateAsset materialTemplate(std::move(info));
    auto layout = device.createBindingLayout({{{0, rhi::BindingType::UniformBuffer, 1, 2},
                                               {1, rhi::BindingType::SampledTexture, 1, 2},
                                               {2, rhi::BindingType::Sampler, 1, 2}}});
    auto parameters = device.createBuffer(
        {16, rhi::flag(rhi::BufferUsage::Uniform), rhi::flag(rhi::MemoryProperty::HostVisible)});
    render::TextureResource first, second;
    const asset::TextureAsset pixels(textureInfo(std::byte{1}));
    first.allocate(device, render::makeTextureCreateInfo(pixels));
    second.allocate(device, render::makeTextureCreateInfo(pixels));
    auto original = render::MaterialBinding::fromParameters(device, materialTemplate, {&first},
                                                            layout, {parameters, {0, 1}, {}});
    const auto originalSet = original.descriptorSet();
    auto replacement = original.withTextures(device, materialTemplate, {&second}, layout);
    require(original.descriptorSet() == originalSet && replacement.descriptorSet() != originalSet &&
                replacement.parameterBuffer() == original.parameterBuffer(),
            "texture rebind mutated a published set or reallocated immutable parameters");
    const auto &boundTexture = std::get<rhi::TextureBinding>(
        replacement.descriptorSet()->description().writes.at(1).resource);
    require(boundTexture.view == second.view(),
            "new material binding did not capture replacement texture");
    device.failBindings = true;
    bool failed = false;
    try
    {
        static_cast<void>(original.withTextures(device, materialTemplate, {&second}, layout));
    }
    catch (const rhi::ResourceError &)
    {
        failed = true;
    }
    require(failed && original.descriptorSet() == originalSet,
            "failed descriptor creation changed the published material");
}
} // namespace
int main()
{
    try
    {
        testCancellationAndTransferFailure();
        testImmutableMaterialTextureBindings();
        testSessionOwnershipAndSharedWork();
        std::cout << "Resource preparation CPU tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
