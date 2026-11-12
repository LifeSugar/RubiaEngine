#include "core/PresentationFrame.hpp"
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace
{
using namespace rubia;
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F&& action)
{
    try { action(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("expected frame failure");
}
// Models a default framebuffer: deliberately has no TextureRef or image index.
struct Frame final : rhi::AcquiredFrame, rhi::PassTarget
{
    explicit Frame(int& abandoned) : abandoned(abandoned) {}
    ~Frame() override { if (!presented) ++abandoned; }
    int& abandoned;
    bool presented = false;
    rhi::OutputDescription description{640, 480, rhi::Format::BGRA8UNorm,
        rhi::OutputColorSpace::SrgbNonlinear};
    const rhi::PassTarget& target() const noexcept override { return *this; }
    const rhi::OutputDescription& output() const noexcept override { return description; }
    rhi::DeviceId deviceId() const noexcept override { return 1; }
    uint32_t width() const noexcept override { return 640; }
    uint32_t height() const noexcept override { return 480; }
};
struct Backend final : rhi::PresentationSurface, rhi::FrameExecutor, rhi::CommandEncoder
{
    render::FrameScheduler& scheduler;
    explicit Backend(render::FrameScheduler& s) : scheduler(s) {}
    bool skip = false, failSubmit = false, failPresent = false;
    int abandoned = 0, submits = 0, presents = 0;
    uint32_t prepared = 99;
    rhi::PresentationStatus acquireStatus = rhi::PresentationStatus::Ready;
    rhi::PresentationStatus presentStatus = rhi::PresentationStatus::Ready;
    void configure(const rhi::PresentationConfig&) override {}
    rhi::OutputDescription output() const noexcept override { return {}; }
    rhi::AcquireResult acquire(uint32_t slot) override
    {
        require(slot == prepared, "acquire preceded slot preparation");
        require(scheduler.pending().size() == 1, "acquire released pending retirement");
        return {acquireStatus, skip ? nullptr : std::make_unique<Frame>(abandoned)};
    }
    uint32_t slotCount() const noexcept override { return 2; }
    void prepareSlot(uint32_t slot) override { prepared = slot; }
    rhi::CommandEncoder& beginCommands(rhi::AcquiredFrame&) override { return *this; }
    void endCommands(rhi::AcquiredFrame&) override {}
    void submit(rhi::AcquiredFrame&) override
    {
        require(!scheduler.lastSubmitted(), "retirement committed before GPU submission");
        if (failSubmit) throw std::runtime_error("injected submit failure");
        ++submits;
    }
    rhi::PresentationStatus present(rhi::AcquiredFrame& frame) override
    {
        require(scheduler.lastSubmitted() && scheduler.pending().empty(),
            "presentation preceded submission commit");
        ++presents;
        if (failPresent) throw std::runtime_error("injected present failure");
        static_cast<Frame&>(frame).presented = true;
        return presentStatus;
    }
    void resetCommandsAfterIdle() override {}
    rhi::RenderEncoder& beginPass(const rhi::RenderPassDesc&) override
    { throw std::runtime_error("unused fake pass"); }
    void endPass() override {}
    void transition(const rhi::TextureRef&, const rhi::SubresourceRange&,
        rhi::ResourceAccess, rhi::ResourceAccess) override {}
};
void runCase(rhi::PresentationStatus status, bool skip, int failure)
{
    render::FrameScheduler scheduler(2);
    Backend backend(scheduler);
    backend.skip = skip;
    backend.acquireStatus = skip ? status : rhi::PresentationStatus::Ready;
    backend.presentStatus = status;
    backend.failSubmit = failure == 2;
    backend.failPresent = failure == 3;
    bool destroyed = false;
    scheduler.pending().retire(std::shared_ptr<const void>(new int(1), [&](const void* p)
        { delete static_cast<const int*>(p); destroyed = true; }));
    bool recorded = false;
    auto execute = [&]
    {
        return render::executePresentationFrame(backend, backend, scheduler, 1,
            [&](rhi::AcquiredFrame& frame, rhi::CommandEncoder&)
            {
                recorded = true;
                require(frame.target().width() == frame.output().width, "output lost framebuffer extent");
                if (failure == 1) throw std::runtime_error("injected recording failure");
            });
    };
    if (failure) rejects(execute);
    else
    {
        const auto result = execute();
        require(result.submitted == !skip && result.status == status, "lost presentation status");
    }
    const bool submitted = !skip && (failure == 0 || failure == 3);
    require(recorded == !skip && backend.submits == int(submitted), "incorrect record/submit sequence");
    require(bool(scheduler.lastSubmitted()) == submitted && !destroyed, "incorrect lifetime commit");
    require(backend.abandoned == int(failure != 0), "failed lease was not abandoned");
    scheduler.beginReusableSlot(0);
    require(!destroyed, "unrelated CPU slot released resources");
    scheduler.beginReusableSlot(1);
    require(destroyed == submitted, "completed submission did not retire its resources");
    scheduler.collectAfterIdle();
    require(destroyed, "idle drain leaked pending resources");
}
void testSuboptimalAcquire()
{
    render::FrameScheduler scheduler(2);
    Backend backend(scheduler);
    scheduler.pending().retire(std::make_shared<int>(1));
    backend.acquireStatus = rhi::PresentationStatus::Suboptimal;
    const auto result = render::executePresentationFrame(backend, backend, scheduler, 0,
        [](rhi::AcquiredFrame&, rhi::CommandEncoder&) {});
    require(result.submitted && result.status == rhi::PresentationStatus::Suboptimal,
        "successful present erased suboptimal acquisition");
}
}
int main()
{
    static_assert(!std::is_copy_constructible_v<Frame>);
    try
    {
        for (auto status : {rhi::PresentationStatus::Unavailable, rhi::PresentationStatus::NeedsReconfigure,
                 rhi::PresentationStatus::SurfaceLost}) runCase(status, true, 0);
        for (auto status : {rhi::PresentationStatus::Ready, rhi::PresentationStatus::Suboptimal,
                 rhi::PresentationStatus::NeedsReconfigure, rhi::PresentationStatus::SurfaceLost})
            runCase(status, false, 0);
        for (int failure = 1; failure <= 3; ++failure) runCase(rhi::PresentationStatus::Ready, false, failure);
        testSuboptimalAcquire();
        std::cout << "Presentation frame contracts passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
