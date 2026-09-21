#include "vulkan/VulkanPipelinePreparation.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace rubia::test
{
void runPipelinePreparationTests(const rhi::vulkan::Device &device,
                                 const rhi::vulkan::GraphicsPipeline::CreateInfo &description)
{
    using namespace rhi::vulkan;
    using render::PipelinePreparationState;
    auto require = [](bool value, const char *message) {
        if (!value)
            throw std::runtime_error(message);
    };
    GraphicsPipelineCache cache(device);
    VulkanPipelinePreparation requests(device, cache, 3, 1);
    const auto waitReady = [&](render::PipelinePreparationTicket ticket) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!render::pipelinePreparationFinished(requests.status(ticket).state) &&
               std::chrono::steady_clock::now() < deadline)
        {
            requests.advance();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(requests.status(ticket).state == PipelinePreparationState::Ready,
                "worker pipeline preparation failed or timed out");
    };
    auto a = description;
    auto b = a;
    b.rasterization.cullMode = VK_CULL_MODE_FRONT_BIT;
    auto c = a;
    c.rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
    auto first = requests.request({a, a, b});
    auto shared = requests.request({a});
    auto third = requests.request({c});
    require(first.accepted() && shared.accepted() && third.accepted() &&
                first.ticket.value != shared.ticket.value &&
                requests.status(first.ticket).total == 2 &&
                shared.disposition == render::ResourcePreparationDisposition::Shared &&
                cache.statistics().creations == 0,
            "prewarm admission failed deduplication/sharing or compiled synchronously");
    require(requests.request({b}).code == render::ResourcePreparationCode::QueueFull,
            "pipeline ticket capacity ignored");
    bool releaseRejected = false;
    try
    {
        requests.release(shared.ticket);
    }
    catch (const std::logic_error &)
    {
        releaseRejected = true;
    }
    require(releaseRejected, "nonterminal pipeline ticket was released");
    requests.cancel(first.ticket);
    requests.release(first.ticket);
    requests.advance();
    require(requests.status(shared.ticket).state == PipelinePreparationState::Preparing &&
                requests.status(third.ticket).state == PipelinePreparationState::Queued &&
                cache.statistics().creations == 0 && !cache.find(b),
            "dispatch published synchronously or exceeded in-flight budget");
    waitReady(shared.ticket);
    require(cache.statistics().creations == 1 && !cache.find(b),
            "independent subscriber cancellation failed");
    waitReady(third.ticket);
    require(cache.statistics().creations == 2, "queued worker pipeline did not complete");
    const auto hit = requests.request({a, c});
    require(hit.accepted() && hit.disposition == render::ResourcePreparationDisposition::CacheHit &&
                requests.status(hit.ticket).completed == 2 && cache.statistics().creations == 2,
            "resident pipelines were rebuilt");
    requests.release(hit.ticket);
    requests.release(shared.ticket);
    requests.release(third.ticket);
    const auto allCancelled = requests.request({b});
    requests.cancelAll();
    requests.advance();
    require(requests.status(allCancelled.ticket).state == PipelinePreparationState::Cancelled &&
                !cache.find(b),
            "cancelled pipeline request still executed");
    requests.release(allCancelled.ticket);
    auto invalid = a;
    invalid.renderPass.reset();
    require(!requests.request({b, invalid}).accepted(), "invalid pipeline batch admitted");
    requests.advance();
    require(!cache.find(b), "failed admission left partial work queued");
    auto retry = requests.request({b});
    b.rasterization.cullMode = VK_CULL_MODE_NONE;
    waitReady(retry.ticket);
    auto expected = a;
    expected.rasterization.cullMode = VK_CULL_MODE_FRONT_BIT;
    require(requests.status(retry.ticket).state == PipelinePreparationState::Ready &&
                cache.find(expected),
            "cancel/retry lost its immutable description snapshot");
    requests.release(retry.ticket);
    // If drawing fills a miss first, queued execution must reuse that same object.
    auto d = a;
    d.colorBlend.attachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    auto fallback = requests.request({d});
    const auto drawn = cache.getOrCreate(d);
    const auto creations = cache.statistics().creations;
    requests.advance();
    require(requests.status(fallback.ticket).state == PipelinePreparationState::Ready &&
                cache.find(d) == drawn && cache.statistics().creations == creations,
            "draw fallback and queued prewarm created duplicate pipelines");
    requests.release(fallback.ticket);
    // An in-flight cancellation must never populate the cache, even after its
    // ticket has been released. Waiting for a later job drains the old completion.
    auto abandonedInfo = a;
    abandonedInfo.colorBlend.attachments[0].colorWriteMask = VK_COLOR_COMPONENT_A_BIT;
    auto abandoned = requests.request({abandonedInfo});
    requests.advance();
    requests.cancel(abandoned.ticket);
    requests.release(abandoned.ticket);
    auto drainInfo = a;
    drainInfo.colorBlend.attachments[0].colorWriteMask = 0;
    auto drain = requests.request({drainInfo});
    waitReady(drain.ticket);
    require(!cache.find(abandonedInfo), "cancelled in-flight result was published");
    requests.release(drain.ticket);

    // Cancel after dispatch, then request the SAME key. Old completion must neither
    // publish to a cleared scene cache nor erase the new job's deduplication entry.
    auto e = a;
    e.colorBlend.attachments[0].colorWriteMask = VK_COLOR_COMPONENT_G_BIT;
    auto obsolete = requests.request({e});
    requests.advance();
    require(requests.status(obsolete.ticket).state == PipelinePreparationState::Preparing,
            "worker request did not enter Preparing");
    requests.cancelAll();
    requests.release(obsolete.ticket);
    cache.clear();
    auto fresh = requests.request({e});
    auto freshShared = requests.request({e});
    require(freshShared.disposition == render::ResourcePreparationDisposition::Shared,
            "retry after in-flight cancellation was not shared");
    waitReady(fresh.ticket);
    require(requests.status(freshShared.ticket).state == PipelinePreparationState::Ready &&
                cache.size() == 1 && cache.find(e),
            "old completion corrupted new scene preparation");
    requests.release(fresh.ticket);
    requests.release(freshShared.ticket);

    // Drawing can win even after the worker has been dispatched. Publication must
    // retain the draw's object, regardless of the worker's native creation result.
    auto f = a;
    f.colorBlend.attachments[0].colorWriteMask = VK_COLOR_COMPONENT_B_BIT;
    auto racing = requests.request({f});
    requests.advance();
    const auto winner = cache.getOrCreate(f);
    const auto published = cache.statistics().creations;
    waitReady(racing.ticket);
    require(cache.find(f) == winner && cache.statistics().creations == published,
            "worker publication replaced draw fallback cache entry");
    requests.release(racing.ticket);

    // Destruction joins outstanding work while the device and immutable inputs live.
    const auto beforeShutdown = cache.statistics().creations;
    {
        VulkanPipelinePreparation shutdown(device, cache);
        auto pending = shutdown.request({a, c});
        require(pending.accepted(), "shutdown test admission failed");
        shutdown.advance();
    }
    require(cache.statistics().creations == beforeShutdown && !cache.find(a),
            "shutdown published an unfinished request");
    std::clog << "[Vulkan] Pipeline preparation tests passed (worker, budget, sharing, "
                 "cancellation, cache, shutdown)\n";
}
} // namespace rubia::test
