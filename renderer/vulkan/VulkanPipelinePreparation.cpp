#include "vulkan/VulkanPipelinePreparation.hpp"
#include "vulkan/Device.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace rubia::rhi::vulkan
{
using render::PipelinePreparationState;
using render::ResourcePreparationCode;
using render::ResourcePreparationDisposition;
VulkanPipelinePreparation::VulkanPipelinePreparation(const Device &device,
                                                     GraphicsPipelineCache &cache,
                                                     std::size_t maxRequests,
                                                     std::size_t pipelinesPerAdvance)
    : device_(device), cache_(cache), maxRequests_(maxRequests),
      pipelinesPerAdvance_(pipelinesPerAdvance), thread_(std::this_thread::get_id())
{
    if (!maxRequests_ || !pipelinesPerAdvance_)
        throw std::invalid_argument("pipeline preparation capacity/budget must be positive");
    active_.reserve(pipelinesPerAdvance_);
    worker_ = std::thread([this] { runWorker(); });
}
VulkanPipelinePreparation::~VulkanPipelinePreparation()
{
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        stopping_ = true;
        workerQueue_.clear();
    }
    workerWake_.notify_one();
    // A Vulkan creation already running cannot be interrupted. Device remains alive.
    if (worker_.joinable())
        worker_.join();
}
void VulkanPipelinePreparation::runWorker() noexcept
{
    for (;;)
    {
        std::shared_ptr<Work> work;
        {
            std::unique_lock<std::mutex> lock(workerMutex_);
            workerWake_.wait(lock, [this] { return stopping_ || !workerQueue_.empty(); });
            if (stopping_)
                return;
            work = std::move(workerQueue_.front());
            workerQueue_.pop_front();
        }
        std::shared_ptr<const GraphicsPipeline> result;
        std::exception_ptr error;
        try
        {
            if (!work->cancelled.load())
                result = std::make_shared<GraphicsPipeline>(device_, work->description);
        }
        catch (...)
        {
            error = std::current_exception();
        }
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            work->result = std::move(result);
            work->error = error;
            work->done = true;
        }
    }
}
void VulkanPipelinePreparation::eraseJob(const std::shared_ptr<Job> &job)
{
    const auto found = jobs_.find(job->key);
    // A cancelled in-flight job may have a newer retry with exactly the same key.
    if (found != jobs_.end() && found->second == job)
        jobs_.erase(found);
}
void VulkanPipelinePreparation::checkThread() const
{
    if (thread_ != std::this_thread::get_id())
        throw std::logic_error("pipeline preparation must run on its owning render thread");
}
void VulkanPipelinePreparation::finish(Request &request)
{
    for (auto &job : request.jobs)
    {
        if (--job->subscribers == 0 && !render::pipelinePreparationFinished(job->state))
        {
            job->state = PipelinePreparationState::Cancelled;
            eraseJob(job);
            if (job->work)
                job->work->cancelled.store(true);
            job->description = {};
            queue_.erase(std::remove(queue_.begin(), queue_.end(), job), queue_.end());
        }
    }
    request.jobs.clear();
}
void VulkanPipelinePreparation::update(Request &request)
{
    if (render::pipelinePreparationFinished(request.status.state))
        return;
    request.status.completed = 0;
    bool preparing = false;
    for (const auto &job : request.jobs)
    {
        if (job->state == PipelinePreparationState::Failed)
        {
            request.status.state = PipelinePreparationState::Failed;
            request.status.error = job->error;
            finish(request);
            return;
        }
        preparing |= job->state == PipelinePreparationState::Preparing;
        if (job->state == PipelinePreparationState::Ready)
            ++request.status.completed;
    }
    if (request.status.completed == request.status.total)
    {
        request.status.state = PipelinePreparationState::Ready;
        finish(request);
    }
    else if (request.status.completed || preparing)
        request.status.state = PipelinePreparationState::Preparing;
}
render::PipelinePreparationResult VulkanPipelinePreparation::request(
    std::vector<GraphicsPipeline::CreateInfo> descriptions)
{
    checkThread();
    if (descriptions.empty())
        return {ResourcePreparationCode::InvalidRequest, {}, "pipeline request is empty"};
    if (requests_.size() >= maxRequests_)
        return {ResourcePreparationCode::QueueFull, {}, "pipeline ticket capacity exhausted"};
    if (!nextTicket_)
        throw std::overflow_error("pipeline ticket space exhausted");
    // Validate the complete batch before admitting anything. Never creates a PSO.
    std::vector<GraphicsPipelineKey> keys;
    try
    {
        keys.reserve(descriptions.size());
        for (const auto &info : descriptions)
        {
            GraphicsPipeline::validate(device_, info);
            keys.push_back(GraphicsPipelineKey::from(info));
        }
    }
    catch (const std::exception &error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
    Request request;
    request.jobs.reserve(keys.size());
    bool started = false;
    bool shared = false;
    try
    {
        std::unordered_set<GraphicsPipelineKey, GraphicsPipelineKeyHash> seen;
        for (std::size_t i = 0; i < keys.size(); ++i)
        {
            if (!seen.insert(keys[i]).second)
                continue;
            auto found = jobs_.find(keys[i]);
            std::shared_ptr<Job> job;
            if (found != jobs_.end())
            {
                job = found->second;
                shared = true;
            }
            else
            {
                job = std::make_shared<Job>();
                job->key = keys[i];
                if (cache_.find(descriptions[i]))
                    job->state = PipelinePreparationState::Ready;
                else
                {
                    job->description = std::move(descriptions[i]);
                    queue_.push_back(job);
                    try
                    {
                        jobs_.emplace(job->key, job);
                    }
                    catch (...)
                    {
                        queue_.pop_back();
                        throw;
                    }
                    started = true;
                }
            }
            request.jobs.push_back(job); // Capacity reserved above.
            ++job->subscribers;
        }
        request.status.total = request.jobs.size();
        update(request);
        const auto id = nextTicket_;
        // Copy retains rollback ownership if insertion allocates and throws.
        requests_.emplace(id, request);
        ++nextTicket_;
        return {ResourcePreparationCode::Accepted,
                {id},
                {},
                started  ? ResourcePreparationDisposition::Started
                : shared ? ResourcePreparationDisposition::Shared
                         : ResourcePreparationDisposition::CacheHit};
    }
    catch (const std::exception &error)
    {
        finish(request);
        return {ResourcePreparationCode::Failed, {}, error.what()};
    }
}
render::PipelinePreparationStatus VulkanPipelinePreparation::status(
    render::PipelinePreparationTicket ticket) const
{
    checkThread();
    return requests_.at(ticket.value).status;
}
void VulkanPipelinePreparation::cancel(render::PipelinePreparationTicket ticket)
{
    checkThread();
    auto &request = requests_.at(ticket.value);
    if (render::pipelinePreparationFinished(request.status.state))
        return;
    request.status.state = PipelinePreparationState::Cancelled;
    finish(request);
}
void VulkanPipelinePreparation::release(render::PipelinePreparationTicket ticket)
{
    checkThread();
    if (!render::pipelinePreparationFinished(requests_.at(ticket.value).status.state))
        throw std::logic_error("only terminal pipeline tickets can be released");
    requests_.erase(ticket.value);
}
void VulkanPipelinePreparation::cancelAll()
{
    checkThread();
    for (auto &[id, request] : requests_)
        cancel({id});
    queue_.clear();
    jobs_.clear();
}
void VulkanPipelinePreparation::advance()
{
    checkThread();
    for (auto it = active_.begin(); it != active_.end();)
    {
        auto job = *it;
        auto work = job->work;
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            if (!work->done)
            {
                ++it;
                continue;
            }
        } // Worker never modifies this completed Work again.
        if (job->state != PipelinePreparationState::Cancelled)
        {
            try
            {
                if (work->error)
                    std::rethrow_exception(work->error);
                cache_.publish(job->key, work->result);
                job->state = PipelinePreparationState::Ready;
            }
            catch (const std::exception &error)
            {
                job->state = PipelinePreparationState::Failed;
                job->error = error.what();
            }
            catch (...)
            {
                job->state = PipelinePreparationState::Failed;
                job->error = "unknown pipeline creation failure";
            }
        }
        eraseJob(job);
        job->work.reset();
        it = active_.erase(it);
    }
    std::size_t dispatched = 0;
    while (!queue_.empty() && dispatched < pipelinesPerAdvance_ &&
           active_.size() < pipelinesPerAdvance_)
    {
        auto job = queue_.front();
        queue_.pop_front();
        ++dispatched;
        try
        {
            // Drawing may have filled the cache since admission. No worker work then.
            if (cache_.find(job->description))
            {
                job->state = PipelinePreparationState::Ready;
                eraseJob(job);
            }
            else
            {
                auto work = std::make_shared<Work>();
                work->description = std::move(job->description);
                {
                    std::lock_guard<std::mutex> lock(workerMutex_);
                    workerQueue_.push_back(work);
                }
                job->work = std::move(work);
                job->state = PipelinePreparationState::Preparing;
                active_.push_back(job); // Capacity reserved in constructor.
                workerWake_.notify_one();
            }
        }
        catch (const std::exception &error)
        {
            job->state = PipelinePreparationState::Failed;
            job->error = error.what();
            eraseJob(job);
        }
        job->description = {};
    }
    for (auto &[id, request] : requests_)
        update(request);
}
} // namespace rubia::rhi::vulkan
