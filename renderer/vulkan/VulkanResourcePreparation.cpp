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
bool sameSlot(const ResourcePreparationTarget &a, const ResourcePreparationTarget &b)
{
    return a.cache == b.cache && a.asset.index() == b.asset.index() &&
           std::visit([](auto handle) { return handle.index; }, a.asset) ==
               std::visit([](auto handle) { return handle.index; }, b.asset);
}
} // namespace
VulkanResourcePreparation::VulkanResourcePreparation(const Device &device,
                                                     VulkanUploadService &uploads,
                                                     RetiredResources &retired,
                                                     render::ResourcePreparationOptions options,
                                                     std::size_t maxRequests)
    : device_(device), uploads_(uploads), retired_(retired), maxRequests_(maxRequests),
      options_(options), thread_(std::this_thread::get_id())
{
    if (!maxRequests_)
    {
        throw std::invalid_argument("preparation capacity must be positive");
    }
    if (!render::validMaterialParameterMemory(options_.material.parameterMemory))
        throw std::invalid_argument("invalid material parameter memory policy");
    activeCreations_.reserve(maxCreationsInFlight_);
    creationThread_ = std::thread([this] { runCreationWorker(); });
}
void VulkanResourcePreparation::runCreationWorker() noexcept
{
    for (;;)
    {
        std::shared_ptr<CreationWork> work;
        {
            std::unique_lock<std::mutex> lock(creationMutex_);
            creationWake_.wait(lock, [this] { return stopping_ || !creationQueue_.empty(); });
            if (stopping_)
                return;
            work = std::move(creationQueue_.front());
            creationQueue_.pop_front();
        }
        try
        {
            if (!work->cancelled.load())
            {
                work->preparation->createGpuResources(device_);
                work->upload = work->preparation->buildUploadRequest();
            }
        }
        catch (...)
        {
            work->error = std::current_exception();
        }
        {
            std::lock_guard<std::mutex> lock(creationMutex_);
            work->done = true;
        }
    }
}
bool VulkanResourcePreparation::advanceCreation(PreparationRecord &record)
{
    if (record.created)
        return true;
    if (record.creation)
    {
        auto work = record.creation;
        {
            std::lock_guard<std::mutex> lock(creationMutex_);
            if (!work->done)
                return false;
        }
        if (work->error)
            std::rethrow_exception(work->error);
        record.preparation = std::move(work->preparation);
        record.pendingUpload = std::move(work->upload);
        record.creation.reset();
        record.created = true;
        return true;
    }
    if (activeCreations_.size() >= maxCreationsInFlight_ ||
        dispatchedThisAdvance_ >= maxCreationsInFlight_)
        return false;
    if (!record.target.cache->dependenciesCurrent(record.target))
        throw std::runtime_error("dependency version changed before resource creation");
    record.preparation->captureDependencies();
    auto work = std::make_shared<CreationWork>();
    work->preparation = std::move(record.preparation);
    {
        std::lock_guard<std::mutex> lock(creationMutex_);
        creationQueue_.push_back(work);
    }
    record.creation = work;
    activeCreations_.push_back(std::move(work)); // Constructor reserved capacity.
    ++dispatchedThisAdvance_;
    creationWake_.notify_one();
    return false;
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
    {
        std::lock_guard<std::mutex> lock(creationMutex_);
        stopping_ = true;
        creationQueue_.clear();
    }
    creationWake_.notify_one();
    if (creationThread_.joinable())
        creationThread_.join();
}
void VulkanResourcePreparation::retire(PreparationRecord &record)
{
    if (record.creation)
    {
        record.creation->cancelled.store(true);
        record.creation.reset(); // Active worker keeps exclusive ownership until completion.
    }
    if (record.upload)
    {
        uploads_.cancel(record.upload);
        uploads_.releaseTicket(record.upload);
        record.upload = {};
    }
    for (auto &dependency : record.dependencies)
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
    record.publication.reset();
}
ResourcePreparationResult VulkanResourcePreparation::prepareTexture(
    RenderAssetCache &cache, asset::AssetSnapshot<asset::TextureAsset> source)
{
    checkThread();
    try
    {
        auto target = preparationTarget(cache, source);
        return prepare(std::move(target), makeTexturePreparation(cache, std::move(source)));
    }
    catch (const std::invalid_argument &error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
}
ResourcePreparationResult VulkanResourcePreparation::prepareMesh(
    RenderAssetCache &cache, asset::AssetSnapshot<asset::MeshAsset> source)
{
    checkThread();
    try
    {
        auto target = preparationTarget(cache, source);
        return prepare(std::move(target), makeMeshPreparation(cache, std::move(source)));
    }
    catch (const std::invalid_argument &error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
}
ResourcePreparationResult VulkanResourcePreparation::prepareShader(
    RenderAssetCache &cache, asset::AssetSnapshot<asset::ShaderAsset> source)
{
    checkThread();
    try
    {
        auto target = preparationTarget(cache, source);
        return prepare(std::move(target), makeShaderPreparation(cache, std::move(source)));
    }
    catch (const std::invalid_argument &error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
}
ResourcePreparationResult VulkanResourcePreparation::prepareShaderProgram(
    RenderAssetCache &cache, asset::AssetSnapshot<asset::ShaderProgramAsset> source)
{
    checkThread();
    try
    {
        auto target = preparationTarget(cache, source);
        return prepare(std::move(target), makeShaderProgramPreparation(cache, std::move(source)));
    }
    catch (const std::invalid_argument &error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
}
ResourcePreparationResult VulkanResourcePreparation::prepareMaterialTemplate(
    RenderAssetCache &cache, asset::AssetSnapshot<asset::MaterialTemplateAsset> source)
{
    checkThread();
    try
    {
        auto target = preparationTarget(cache, source);
        return prepare(std::move(target),
                       makeMaterialTemplatePreparation(cache, std::move(source)));
    }
    catch (const std::invalid_argument &error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
}
ResourcePreparationResult VulkanResourcePreparation::prepareMaterial(
    RenderAssetCache &cache, asset::AssetSnapshot<asset::MaterialAsset> source)
{
    checkThread();
    try
    {
        auto target = preparationTarget(cache, source);
        return prepare(std::move(target),
                       makeMaterialPreparation(cache, std::move(source), options_.material));
    }
    catch (const std::invalid_argument &error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
}
ResourcePreparationResult VulkanResourcePreparation::prepareAny(
    RenderAssetCache &cache, render::ResourceAssetSnapshot source)
{
    struct Restore
    {
        bool &flag;
        bool previous;
        ~Restore()
        {
            flag = previous;
        }
    } restore{dependencyAdmission_, dependencyAdmission_};
    dependencyAdmission_ = true;
    return std::visit(
        [&](auto value) -> ResourcePreparationResult {
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
    catch (const std::exception &error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
    const auto externalCount = static_cast<std::size_t>(std::count_if(
        records_.begin(), records_.end(), [](const auto &pair) { return !pair.second.internal; }));
    if (!dependencyAdmission_ && externalCount >= maxRequests_)
    {
        return {ResourcePreparationCode::QueueFull, {}, "preparation ticket capacity exhausted"};
    }
    // Separate internal capacity prevents a parent consuming the only slot needed by its child.
    // Direct dependencies are advanced sequentially; the render-resource graph has bounded depth.
    constexpr auto slotsPerExternalRequest = std::variant_size_v<render::ResourceAssetSnapshot> + 1;
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
        for (const auto &wanted : target.dependencies)
        {
            for (const auto &previous : versions.requestedDependencies)
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
    for (const auto &pair : tasks_)
    {
        auto task = pair.second;
        if (sameSlot(task->target, target) && task->target.asset == target.asset &&
            task->target.domain == target.domain && task->target.revision == target.revision &&
            task->target.dependencies == target.dependencies &&
            !resourcePreparationFinished(taskStatus(*task).state))
        {
            if (task->publication)
                return {ResourcePreparationCode::InvalidRequest, {}, "resource has an exclusive publication transaction"};
            records_.emplace(id, Subscription{task, {}, dependencyAdmission_});
            ++task->subscribers;
            ++nextTicket_;
            return {ResourcePreparationCode::Accepted,
                    {id},
                    {},
                    ResourcePreparationDisposition::Shared};
        }
    }
    const auto dependencies = preparation->dependencies();
    auto task = std::make_shared<PreparationRecord>();
    task->target = target;
    task->preparation = std::move(preparation);
    task->subscribers = 1;
    for (const auto &source : dependencies)
    {
        task->dependencies.push_back({source, {}, false});
    }
    records_.emplace(id, Subscription{task, {}, dependencyAdmission_});
    try
    {
        tasks_.emplace(id, task);
        // Admission is CPU-only; object creation and upload admission happen later.
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
        auto &old = *it->second;
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
    const PreparationRecord &record) const
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
    const auto &subscription = records_.at(ticket.value);
    return subscription.cancelled ? *subscription.cancelled : taskStatus(*subscription.task);
}
bool VulkanResourcePreparation::advanceDependencies(PreparationRecord &record)
{
    for (auto &dependency : record.dependencies)
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
    dispatchedThisAdvance_ = 0;
    {
        std::lock_guard<std::mutex> lock(creationMutex_);
        activeCreations_.erase(std::remove_if(activeCreations_.begin(), activeCreations_.end(),
                                              [](const auto &work) { return work->done; }),
                               activeCreations_.end());
    }
    // Newly scheduled dependencies start advancing on the next pump, keeping each pump bounded.
    const auto last = nextTicket_ - 1;
    for (auto it = tasks_.begin(); it != tasks_.end() && it->first <= last;)
    {
        auto &record = *it->second;
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
                    const auto current = record.target.cache->inspectPreparation(record.target);
                    // A texture publication can already have rebuilt these exact
                    // material bindings while preserving their parameter buffer.
                    if (!record.publication && !record.created && !record.creation &&
                        current.resident == record.target.revision &&
                        current.residentDependencies == record.target.dependencies &&
                        record.target.cache->dependenciesCurrent(record.target))
                    {
                        record.status.state = ResourcePreparationState::Ready;
                    }
                    else if (advanceCreation(record))
                    {
                        if (!record.target.cache->dependenciesCurrent(record.target))
                            throw std::runtime_error(
                                "dependency version changed during resource creation");
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
                            try
                            {
                                if (record.publication) record.publication->begin();
                                record.preparation->publish(retired_);
                                if (record.publication) record.publication->commit();
                            }
                            catch (...)
                            {
                                if (record.publication) record.publication->rollback();
                                throw;
                            }
                            record.status.state = ResourcePreparationState::Ready;
                        }
                    }
                }
            }
            catch (const std::exception &error)
            {
                record.status.state = ResourcePreparationState::Failed;
                record.status.error = error.what();
            }
            catch (...)
            {
                record.status.state = ResourcePreparationState::Failed;
                record.status.error = "unknown resource creation failure";
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
    auto &subscription = records_.at(ticket.value);
    auto current = status(ticket);
    if (resourcePreparationFinished(current.state))
    {
        return;
    }
    current.state = ResourcePreparationState::Cancelled;
    subscription.cancelled = std::move(current);
    auto &task = *subscription.task;
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
void VulkanResourcePreparation::setPublicationTransaction(ResourcePreparationTicket ticket,
    std::shared_ptr<render::ResourcePublicationTransaction> publication)
{
    checkThread();
    auto& subscription = records_.at(ticket.value);
    auto& task = *subscription.task;
    if (!publication || subscription.cancelled || task.subscribers != 1 || task.created ||
        task.creation || task.publication || resourcePreparationFinished(task.status.state))
        throw std::logic_error("publication transaction requires an exclusive unstarted task");
    task.publication = std::move(publication);
}
} // namespace rubia::rhi::vulkan
