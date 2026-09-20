#include "vulkan/VulkanResourcePreparation.hpp"
#include "vulkan/RenderAssetCache.hpp"
#include "vulkan/TextureUploadBuilder.hpp"
#include "vulkan/UploadValidation.hpp"
#include "vulkan/VulkanUploadService.hpp"
#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace rubia::rhi::vulkan
{
namespace
{
bool sameSlot(const ResourcePreparationTarget& a, const ResourcePreparationTarget& b)
{
    return a.cache == b.cache && a.asset.index() == b.asset.index() &&
           std::visit([](auto handle) { return handle.index; }, a.asset) ==
               std::visit([](auto handle) { return handle.index; }, b.asset);
}
ResourcePreparationCode preparationCode(UploadEnqueueCode code)
{
    switch (code)
    {
    case UploadEnqueueCode::Accepted:
        return ResourcePreparationCode::Accepted;
    case UploadEnqueueCode::QueueFull:
        return ResourcePreparationCode::QueueFull;
    case UploadEnqueueCode::InvalidRequest:
        return ResourcePreparationCode::InvalidRequest;
    case UploadEnqueueCode::UnsupportedRequest:
        return ResourcePreparationCode::UnsupportedRequest;
    default:
        return ResourcePreparationCode::Failed;
    }
}
} // namespace
VulkanResourcePreparation::VulkanResourcePreparation(const Device& device,
                                                     VulkanUploadService& uploads,
                                                     std::size_t maxRequests)
    : device_(device), uploads_(uploads), maxRequests_(maxRequests),
      thread_(std::this_thread::get_id())
{
    if (!maxRequests_)
    {
        throw std::invalid_argument("preparation capacity must be positive");
    }
}
void VulkanResourcePreparation::checkThread() const
{
    if (std::this_thread::get_id() != thread_)
    {
        throw std::logic_error("resource preparation must be used on its owning render thread");
    }
}
VulkanResourcePreparation::~VulkanResourcePreparation()
{
    checkThread();
    while (!tasks_.empty())
    {
        auto record = tasks_.begin()->second;
        tasks_.erase(tasks_.begin());
        retire(*record);
    }
}
void VulkanResourcePreparation::retire(PreparationRecord& record)
{
    if (record.upload)
    {
        uploads_.cancel(record.upload);
        uploads_.releaseTicket(record.upload);
        record.upload = {};
    }
    for (auto& dependency : record.dependencies)
    {
        if (dependency.ticket)
        {
            cancel(dependency.ticket);
            release(dependency.ticket);
            dependency.ticket = {};
        }
    }
    record.dependencies.clear();
    record.pendingUpload.operations.clear();
    record.preparation.reset();
}
ResourcePreparationResult VulkanResourcePreparation::prepareTexture(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::TextureAsset> source)
{
    checkThread();
    try
    {
        auto target = preparationTarget(cache, source);
        return prepare(std::move(target), makeTexturePreparation(cache, std::move(source)));
    }
    catch (const std::invalid_argument& error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
}
ResourcePreparationResult VulkanResourcePreparation::prepareMesh(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::MeshAsset> source)
{
    checkThread();
    try
    {
        auto target = preparationTarget(cache, source);
        return prepare(std::move(target), makeMeshPreparation(cache, std::move(source)));
    }
    catch (const std::invalid_argument& error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
}
ResourcePreparationResult VulkanResourcePreparation::prepareShader(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::ShaderAsset> source)
{
    checkThread();
    try
    {
        auto target = preparationTarget(cache, source);
        return prepare(std::move(target), makeShaderPreparation(cache, std::move(source)));
    }
    catch (const std::invalid_argument& error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
}
ResourcePreparationResult VulkanResourcePreparation::prepareShaderProgram(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::ShaderProgramAsset> source)
{
    checkThread();
    try
    {
        auto target = preparationTarget(cache, source);
        return prepare(std::move(target), makeShaderProgramPreparation(cache, std::move(source)));
    }
    catch (const std::invalid_argument& error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
}
ResourcePreparationResult VulkanResourcePreparation::prepareMaterialTemplate(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::MaterialTemplateAsset> source)
{
    checkThread();
    try
    {
        auto target = preparationTarget(cache, source);
        return prepare(std::move(target),
                       makeMaterialTemplatePreparation(cache, std::move(source)));
    }
    catch (const std::invalid_argument& error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
}
ResourcePreparationResult VulkanResourcePreparation::prepareMaterial(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::MaterialAsset> source)
{
    checkThread();
    try
    {
        auto target = preparationTarget(cache, source);
        return prepare(std::move(target), makeMaterialPreparation(cache, std::move(source)));
    }
    catch (const std::invalid_argument& error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
}
ResourcePreparationResult VulkanResourcePreparation::prepareModel(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::ModelAsset> source)
{
    checkThread();
    try
    {
        auto target = preparationTarget(cache, source);
        return prepare(std::move(target), makeModelPreparation(cache, std::move(source)));
    }
    catch (const std::invalid_argument& error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
}
ResourcePreparationResult VulkanResourcePreparation::prepareAny(RenderAssetCache& cache,
                                                                asset::AnyAssetSnapshot source)
{
    struct Restore
    {
        bool& flag;
        bool previous;
        ~Restore()
        {
            flag = previous;
        }
    } restore{dependencyAdmission_, dependencyAdmission_};
    dependencyAdmission_ = true;
    return std::visit(
        [&](auto value) -> ResourcePreparationResult
        {
            using Snapshot = decltype(value);
            if constexpr (std::is_same_v<Snapshot, asset::AssetSnapshot<asset::TextureAsset>>)
            {
                return prepareTexture(cache, std::move(value));
            }
            else if constexpr (std::is_same_v<Snapshot, asset::AssetSnapshot<asset::MeshAsset>>)
            {
                return prepareMesh(cache, std::move(value));
            }
            else if constexpr (std::is_same_v<Snapshot, asset::AssetSnapshot<asset::ShaderAsset>>)
            {
                return prepareShader(cache, std::move(value));
            }
            else if constexpr (std::is_same_v<Snapshot,
                                              asset::AssetSnapshot<asset::ShaderProgramAsset>>)
            {
                return prepareShaderProgram(cache, std::move(value));
            }
            else if constexpr (std::is_same_v<Snapshot,
                                              asset::AssetSnapshot<asset::MaterialTemplateAsset>>)
            {
                return prepareMaterialTemplate(cache, std::move(value));
            }
            else if constexpr (std::is_same_v<Snapshot, asset::AssetSnapshot<asset::MaterialAsset>>)
            {
                return prepareMaterial(cache, std::move(value));
            }
            else if constexpr (std::is_same_v<Snapshot, asset::AssetSnapshot<asset::ModelAsset>>)
            {
                return prepareModel(cache, std::move(value));
            }
        },
        std::move(source));
}
ResourcePreparationResult VulkanResourcePreparation::prepare(
    ResourcePreparationTarget target, std::unique_ptr<IResourcePreparation> preparation)
{
    checkThread();
    if (!preparation || !target.cache)
    {
        return {
            ResourcePreparationCode::InvalidRequest, {}, "invalid preparation target or operation"};
    }
    RenderAssetCache::PreparationVersions versions;
    try
    {
        versions = target.cache->inspectPreparation(target);
    }
    catch (const std::exception& error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
    const auto externalCount = static_cast<std::size_t>(std::count_if(
        records_.begin(), records_.end(), [](const auto& pair) { return !pair.second.internal; }));
    if (!dependencyAdmission_ && externalCount >= maxRequests_)
    {
        return {ResourcePreparationCode::QueueFull, {}, "preparation ticket capacity exhausted"};
    }
    // Separate internal capacity prevents a parent consuming the only slot needed by its child.
    // Direct dependencies are advanced sequentially; the seven-type graph has bounded depth.
    constexpr auto slotsPerExternalRequest = std::variant_size_v<asset::AnyAssetSnapshot> + 1;
    if (dependencyAdmission_ && records_.size() / slotsPerExternalRequest >= maxRequests_)
    {
        return {ResourcePreparationCode::Failed, {}, "dependency subscription capacity exhausted"};
    }
    if (!nextTicket_)
    {
        throw std::overflow_error("preparation ticket space exhausted");
    }
    const auto id = nextTicket_;
    if (versions.resident == target.revision &&
        versions.residentDependencies == target.dependencies &&
        target.cache->dependenciesCurrent(target))
    {
        auto task = std::make_shared<PreparationRecord>();
        task->target = target;
        task->status.state = ResourcePreparationState::Ready;
        records_.emplace(id, Subscription{task, {}, dependencyAdmission_});
        ++nextTicket_;
        return {
            ResourcePreparationCode::Accepted, {id}, {}, ResourcePreparationDisposition::CacheHit};
    }
    if (target.revision < versions.requested)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, "stale preparation revision"};
    }
    if (target.revision == versions.requested)
    {
        for (const auto& wanted : target.dependencies)
        {
            for (const auto& previous : versions.requestedDependencies)
            {
                if (wanted.handle == previous.handle && wanted.revision < previous.revision)
                {
                    return {ResourcePreparationCode::InvalidRequest,
                            {},
                            "stale preparation dependency version"};
                }
            }
        }
    }
    for (const auto& pair : tasks_)
    {
        auto task = pair.second;
        if (sameSlot(task->target, target) && task->target.asset == target.asset &&
            task->target.domain == target.domain && task->target.revision == target.revision &&
            task->target.dependencies == target.dependencies &&
            !resourcePreparationFinished(taskStatus(*task).state))
        {
            records_.emplace(id, Subscription{task, {}, dependencyAdmission_});
            ++task->subscribers;
            ++nextTicket_;
            return {ResourcePreparationCode::Accepted,
                    {id},
                    {},
                    ResourcePreparationDisposition::Shared};
        }
    }
    UploadRequest request;
    const auto dependencies = preparation->dependencies();
    try
    {
        if (dependencies.empty())
        {
            preparation->createGpuResources(device_);
            request = preparation->buildUploadRequest();
        }
    }
    catch (const UnsupportedUpload& error)
    {
        return {ResourcePreparationCode::UnsupportedRequest, {}, error.what()};
    }
    catch (const std::invalid_argument& error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
    catch (const std::exception& error)
    {
        return {ResourcePreparationCode::Failed, {}, error.what()};
    }

    auto task = std::make_shared<PreparationRecord>();
    task->target = target;
    task->preparation = std::move(preparation);
    task->subscribers = 1;
    task->created = dependencies.empty();
    for (const auto& source : dependencies)
    {
        task->dependencies.push_back({source, {}, false});
    }
    records_.emplace(id, Subscription{task, {}, dependencyAdmission_});
    try
    {
        tasks_.emplace(id, task);
        if (!request.operations.empty())
        {
            auto result = uploads_.tryEnqueue(request);
            if (!result.accepted())
            {
                tasks_.erase(id);
                records_.erase(id);
                return {preparationCode(result.code), {}, std::move(result.error)};
            }
            task->upload = result.ticket;
            task->status.totalBytes = uploads_.query(result.ticket).totalBytes;
        }
        // Leaf uploads commit after admission. Dependency tasks commit admission now and enqueue
        // their own transfer later; either way, the old resident object survives until publication.
        target.cache->acceptPreparation(target);
    }
    catch (...)
    {
        retire(*task);
        tasks_.erase(id);
        records_.erase(id);
        throw;
    }
    ++nextTicket_;
    // A newer accepted version supersedes older pending work for this slot. It never replaces
    // the resident GPU object until publication succeeds. Submitted Parts retain old upload
    // objects.
    for (auto it = tasks_.begin(); it != tasks_.end();)
    {
        auto& old = *it->second;
        if (it->first != id && sameSlot(old.target, target))
        {
            old.status = taskStatus(old);
            if (!resourcePreparationFinished(old.status.state))
            {
                old.status.state = ResourcePreparationState::Cancelled;
                old.status.error = "superseded by a newer preparation";
            }
            retire(old);
            it = tasks_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return {ResourcePreparationCode::Accepted, {id}, {}, ResourcePreparationDisposition::Started};
}
ResourcePreparationStatus VulkanResourcePreparation::taskStatus(
    const PreparationRecord& record) const
{
    auto result = record.status;
    if (resourcePreparationFinished(result.state) || !record.upload)
    {
        return result;
    }
    const auto upload = uploads_.query(record.upload);
    result.totalBytes = upload.totalBytes;
    result.submittedBytes = upload.submittedBytes;
    result.completedBytes = upload.completedBytes;
    result.error = upload.error;
    switch (upload.state)
    {
    case UploadState::Queued:
        result.state = ResourcePreparationState::Preparing;
        break;
    case UploadState::Uploading:
    case UploadState::Completed:
        result.state = ResourcePreparationState::Uploading;
        break;
    case UploadState::Failed:
        result.state = ResourcePreparationState::Failed;
        break;
    case UploadState::Cancelled:
        result.state = ResourcePreparationState::Cancelled;
        break;
    }
    return result;
}
ResourcePreparationStatus VulkanResourcePreparation::status(ResourcePreparationTicket ticket) const
{
    checkThread();
    const auto& subscription = records_.at(ticket.value);
    return subscription.cancelled ? *subscription.cancelled : taskStatus(*subscription.task);
}
bool VulkanResourcePreparation::advanceDependencies(PreparationRecord& record)
{
    for (auto& dependency : record.dependencies)
    {
        if (dependency.ready)
        {
            continue;
        }
        if (!dependency.ticket)
        {
            const auto result = prepareAny(*record.target.cache, dependency.source);
            if (result.code == ResourcePreparationCode::QueueFull)
            {
                return false;
            }
            if (!result.accepted())
            {
                throw std::runtime_error("dependency preparation rejected: " + result.error);
            }
            dependency.ticket = result.ticket;
        }
        const auto progress = status(dependency.ticket);
        if (progress.state == ResourcePreparationState::Ready)
        {
            release(dependency.ticket);
            dependency.ticket = {};
            dependency.ready = true;
        }
        else if (progress.state == ResourcePreparationState::Failed ||
                 progress.state == ResourcePreparationState::Cancelled)
        {
            throw std::runtime_error("dependency preparation failed/cancelled: " + progress.error);
        }
        else
        {
            return false;
        }
    }
    return true;
}
void VulkanResourcePreparation::advance()
{
    checkThread();
    // Newly scheduled dependencies start advancing on the next pump, keeping each pump bounded.
    const auto last = nextTicket_ - 1;
    for (auto it = tasks_.begin(); it != tasks_.end() && it->first <= last;)
    {
        auto& record = *it->second;
        record.status = taskStatus(record);
        if (!resourcePreparationFinished(record.status.state))
        {
            try
            {
                const auto versions = record.target.cache->inspectPreparation(record.target);
                if (versions.requested != record.target.revision ||
                    versions.requestedDependencies != record.target.dependencies)
                {
                    record.status.state = ResourcePreparationState::Cancelled;
                    record.status.error = "preparation target changed before publication";
                }
                else if (advanceDependencies(record))
                {
                    if (!record.created)
                    {
                        if (!record.target.cache->dependenciesCurrent(record.target))
                        {
                            throw std::runtime_error(
                                "dependency version changed before resource creation");
                        }
                        record.preparation->createGpuResources(device_);
                        record.pendingUpload = record.preparation->buildUploadRequest();
                        record.created = true;
                    }
                    if (!record.pendingUpload.operations.empty())
                    {
                        const auto result = uploads_.tryEnqueue(record.pendingUpload);
                        if (result.accepted())
                        {
                            record.upload = result.ticket;
                        }
                        else if (result.code != UploadEnqueueCode::QueueFull)
                        {
                            throw std::runtime_error(result.error);
                        }
                    }
                    record.status = taskStatus(record);
                    if (record.pendingUpload.operations.empty() &&
                        (!record.upload ||
                         uploads_.query(record.upload).state == UploadState::Completed))
                    {
                        if (!record.target.cache->dependenciesCurrent(record.target))
                        {
                            throw std::runtime_error(
                                "dependency version changed before publication");
                        }
                        record.preparation->publish();
                        record.status.state = ResourcePreparationState::Ready;
                    }
                }
            }
            catch (const std::exception& error)
            {
                record.status.state = ResourcePreparationState::Failed;
                record.status.error = error.what();
            }
        }
        if (resourcePreparationFinished(record.status.state))
        {
            retire(record);
            it = tasks_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
void VulkanResourcePreparation::cancel(ResourcePreparationTicket ticket)
{
    checkThread();
    auto& subscription = records_.at(ticket.value);
    auto current = status(ticket);
    if (resourcePreparationFinished(current.state))
    {
        return;
    }
    current.state = ResourcePreparationState::Cancelled;
    subscription.cancelled = std::move(current);
    auto& task = *subscription.task;
    if (--task.subscribers == 0)
    {
        task.status = *subscription.cancelled;
        retire(task);
        for (auto it = tasks_.begin(); it != tasks_.end(); ++it)
        {
            if (it->second == subscription.task)
            {
                tasks_.erase(it);
                break;
            }
        }
    }
}
void VulkanResourcePreparation::release(ResourcePreparationTicket ticket)
{
    checkThread();
    if (!resourcePreparationFinished(status(ticket).state))
    {
        throw std::logic_error("resource preparation is not finished");
    }
    records_.erase(ticket.value);
}
GpuTexture VulkanResourcePreparation::uploadTextureAndWait(
    std::shared_ptr<const asset::TextureAsset> source)
{
    checkThread();
    if (!source || !*source)
    {
        throw std::logic_error("blocking texture upload requires valid assets");
    }
    auto texture = std::make_shared<GpuTexture>();
    auto info = makeTextureCreateInfo(*source);
    texture->allocate(device_, info);
    auto request = makeTextureUploadRequest(texture, std::move(source));
    auto result = uploads_.tryEnqueue(request);
    if (result.code == UploadEnqueueCode::QueueFull)
    {
        uploads_.drain();
        result = uploads_.tryEnqueue(request);
    }
    if (!result.accepted())
    {
        throw std::runtime_error(result.error.empty() ? "texture upload queue is full"
                                                      : result.error);
    }
    try
    {
        uploads_.drain();
        const auto status = uploads_.query(result.ticket);
        if (status.state != UploadState::Completed)
        {
            throw std::runtime_error("texture upload failed: " + status.error);
        }
    }
    catch (...)
    {
        uploads_.cancel(result.ticket);
        uploads_.releaseTicket(result.ticket);
        throw;
    }
    uploads_.releaseTicket(result.ticket);
    return std::move(*texture);
}

} // namespace rubia::rhi::vulkan
