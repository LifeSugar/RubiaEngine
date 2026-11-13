#include "core/PresentationFrame.hpp"
#include "vulkan/VulkanPresentation.hpp"
#include "vulkan/VulkanResourceDevice.hpp"
#include "vulkan/VulkanContext.hpp"
#include "vulkan/Window.hpp"
#include <iostream>
#include <stdexcept>

namespace
{
using namespace rubia;
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void clear(rhi::AcquiredFrame& frame, rhi::CommandEncoder& commands)
{
    rhi::RenderPassDesc pass;
    pass.target = &frame.target();
    pass.clears.resize(2);
    pass.clears[0].color = {0.1f, 0.2f, 0.3f, 1};
    pass.clears[1].depthStencil = true;
    commands.beginPass(pass);
    commands.endPass();
}
}
int main()
{
    try
    {
        rhi::vulkan::Window window({320, 240, "Presentation contract test", false});
        rhi::vulkan::VulkanContext::CreateInfo info;
        info.enableValidationLayers = true;
        rhi::vulkan::VulkanContext context(window, info);
        rhi::vulkan::VulkanResourceDevice device(context.device());
        // Reject foreign and malformed artifacts before reaching the Vulkan driver.
        for (const auto& artifact : std::vector<ShaderArtifact>{
                 {{ShaderBackend::Metal, "metallib_2_3"}, ShaderFormat::MetalSource, {1,2,3,4}},
                 {{ShaderBackend::Vulkan, "spirv_1_3"}, ShaderFormat::SpirV, {1,2,3}},
                 {{ShaderBackend::Vulkan, "spirv_1_3"}, ShaderFormat::SpirV, std::vector<uint8_t>(20)}})
        {
            bool rejected = false;
            try { (void)device.createShader({rhi::ShaderStage::Vertex, "main", artifact}); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Vulkan accepted foreign or malformed shader code");
        }
        // One CPU slot and multiple presentation images deliberately differ.
        rhi::vulkan::VulkanPresentation backend(context.device(), device.id(), context.surface(), 1);
        rhi::PresentationSurface& surface = backend;
        rhi::FrameExecutor& executor = backend;
        render::FrameScheduler scheduler(1);
        rhi::PresentationConfig config{320, 240};
        config.preferredPresentMode = rhi::PresentMode::Fifo;
        const auto configure = [&](rhi::PresentationConfig next)
        {
            context.waitIdle();
            executor.resetCommandsAfterIdle();
            scheduler.collectAfterIdle();
            surface.configure(next);
        };
        configure(config);
        require(surface.output().presentMode == rhi::PresentMode::Fifo, "FIFO preference was ignored");
        require(backend.imageCount() > executor.slotCount(), "test requires independent image/slot counts");
        const auto render = [&]
        {
            window.pollEvents();
            const auto result = render::executePresentationFrame(surface, executor, scheduler, 0, clear);
            require(result.submitted, "clear frame was not submitted");
            require(!backend.failed(), "normal presentation poisoned the backend");
            if (result.status != rhi::PresentationStatus::Ready) configure(config);
        };
        for (int i = 0; i < 8; ++i) render();
        configure({0, 0});
        const auto skipped = render::executePresentationFrame(surface, executor, scheduler, 0,
            [](rhi::AcquiredFrame&, rhi::CommandEncoder&) { throw std::runtime_error("suspended frame recorded"); });
        require(!skipped.submitted && skipped.status == rhi::PresentationStatus::Unavailable,
            "zero extent did not suspend acquisition");
        configure(config);
        render();
        // Abandon at each native synchronization boundary. Recovery must not wait
        // on a reset-but-never-submitted fence or reuse a signaled semaphore.
        for (int phase = 0; phase < 3; ++phase)
        {
            executor.prepareSlot(0);
            scheduler.beginReusableSlot(0);
            {
                auto acquired = surface.acquire(0);
                require(bool(acquired.frame), "could not acquire abandonment test frame");
                bool rejected = false;
                try { surface.configure(config); } catch (const std::logic_error&) { rejected = true; }
                require(rejected, "reconfigured a live lease");
                if (phase >= 1)
                {
                    auto& commands = executor.beginCommands(*acquired.frame);
                    clear(*acquired.frame, commands);
                    if (phase == 2)
                    {
                        executor.endCommands(*acquired.frame);
                        const auto point = scheduler.candidate();
                        executor.submit(*acquired.frame);
                        scheduler.commitSuccess(0, point);
                    }
                }
            }
            require(backend.failed(), "abandoned lease permitted native synchronization reuse");
            bool rejected = false;
            try { executor.prepareSlot(0); } catch (const std::logic_error&) { rejected = true; }
            require(rejected, "poisoned frame slot could be reused");
            configure(config);
            render();
        }
        glfwSetWindowSize(window.nativeHandle(), 480, 300);
        window.pollEvents();
        const auto extent = window.framebufferExtent();
        config.width = extent.width;
        config.height = extent.height;
        configure(config);
        require(surface.output().width == extent.width && surface.output().height == extent.height,
            "resized output description is stale");
        for (int i = 0; i < 4; ++i) render();
        context.waitIdle();
        executor.resetCommandsAfterIdle();
        scheduler.collectAfterIdle();
        std::cout << "Vulkan presentation, suspend, resize and abandonment recovery passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
