#include "core/FrameScheduler.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace
{
using rubia::render::FrameScheduler;
using rubia::rhi::CompletionPoint;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <class Exception, class F> void rejects(F&& action, const char* message)
{
    try { action(); }
    catch (const Exception&) { return; }
    throw std::runtime_error(message);
}

struct Resource
{
    explicit Resource(int& destroyed) : destroyed(destroyed) {}
    ~Resource() { ++destroyed; }
    int& destroyed;
};

void retire(FrameScheduler& scheduler, int& destroyed)
{
    scheduler.pending().retire(std::make_unique<Resource>(destroyed));
}

void testFailedAttemptsKeepPending()
{
    FrameScheduler scheduler(2);
    int destroyed = 0;
    retire(scheduler, destroyed);

    // Host waited/reset slot 0, but acquire returned out-of-date. No submit.
    scheduler.beginReusableSlot(0);
    require(!scheduler.lastSubmitted(), "acquisition skip created a submission");
    require(scheduler.pending().size() == 1 && destroyed == 0,
        "acquisition skip released pending owners");

    // Retry acquired successfully but command recording threw. The host must
    // recover its native acquire synchronization before attempting more work.
    scheduler.beginReusableSlot(0);
    const auto beforeFailure = scheduler.candidate();
    require(scheduler.pending().size() == 1 && destroyed == 0,
        "recording failure released pending owners");

    // Queue submission failed: discard the candidate. Only the host knows how
    // to recover the unsignaled native fence; ledger state is still unchanged.
    const auto retry = scheduler.candidate();
    require(retry.session == beforeFailure.session && retry.serial == beforeFailure.serial,
        "unsuccessful submit consumed a sequence number");
    require(!scheduler.lastSubmitted() && !scheduler.completion(0),
        "failed attempt attached resources to a nonexistent submission");
    require(scheduler.pending().size() == 1 && destroyed == 0,
        "submit failure released pending owners");

    // Successful submission followed by presentation failure still owns work.
    scheduler.commitSuccess(0, retry);
    require(scheduler.pending().empty() && destroyed == 0,
        "successful submit destroyed rather than transferred owners");
    require(!scheduler.isComplete(retry), "presentation failure implied GPU completion");

    // Slot 1 is unrelated: waiting/resetting it cannot reclaim slot 0's batch.
    scheduler.beginReusableSlot(1);
    require(destroyed == 0 && !scheduler.isComplete(retry),
        "unsubmitted slot reclaimed another slot's in-flight owners");
    scheduler.beginReusableSlot(0);
    require(destroyed == 1 && scheduler.isComplete(retry),
        "native wait/reset did not reclaim the completed batch");
}

void testPendingBelongsToNewSubmission()
{
    FrameScheduler scheduler(1);
    int oldDestroyed = 0, replacementDestroyed = 0;
    scheduler.beginReusableSlot(0);
    retire(scheduler, oldDestroyed);
    const auto oldPoint = scheduler.candidate();
    scheduler.commitSuccess(0, oldPoint);

    // Replaced resources remain pending even if the previous fence is done.
    retire(scheduler, replacementDestroyed);
    scheduler.beginReusableSlot(0);
    require(oldDestroyed == 1 && replacementDestroyed == 0 && !scheduler.pending().empty(),
        "reused fence associated pending owners with its previous cycle");
    const auto newPoint = scheduler.candidate();
    require(newPoint.serial == oldPoint.serial + 1, "successful submits are not monotonic");
    scheduler.commitSuccess(0, newPoint);
    require(replacementDestroyed == 0 && scheduler.isComplete(oldPoint) && !scheduler.isComplete(newPoint),
        "completion of old submission incorrectly completed its replacement");
    scheduler.beginReusableSlot(0);
    require(replacementDestroyed == 1 && scheduler.isComplete(newPoint),
        "replacement batch survived its protecting submission completion");
}

void testIdleAndOrderedCompletion()
{
    FrameScheduler scheduler(2);
    int firstDestroyed = 0, secondDestroyed = 0, pendingDestroyed = 0;
    scheduler.beginReusableSlot(0);
    retire(scheduler, firstDestroyed);
    const auto first = scheduler.candidate();
    scheduler.commitSuccess(0, first);
    scheduler.beginReusableSlot(1);
    retire(scheduler, secondDestroyed);
    const auto second = scheduler.candidate();
    scheduler.commitSuccess(1, second);

    // One ordered queue means the later fence also establishes older work
    // completed, but we retain each batch until its own commands are reset.
    scheduler.beginReusableSlot(1);
    require(scheduler.isComplete(first) && scheduler.isComplete(second),
        "single-queue completion watermark did not include earlier submissions");
    require(firstDestroyed == 0 && secondDestroyed == 1,
        "completed watermark released owners before their slot reset");
    retire(scheduler, pendingDestroyed);
    scheduler.collectAfterIdle();
    require(firstDestroyed == 1 && pendingDestroyed == 1 && scheduler.pending().empty(),
        "idle collection failed to drain submitted and never-submitted owners");
    require(scheduler.isComplete(first) && scheduler.isComplete(second),
        "idle collection forgot previously returned completion points");

    // Resize/idle does not start another session or reuse old serial values.
    scheduler.beginReusableSlot(0);
    const auto next = scheduler.candidate();
    require(next.session == first.session && next.serial == second.serial + 1,
        "idle collection invalidated completion identity");
    scheduler.commitSuccess(0, next);
    require(!scheduler.isComplete(next), "new work inherited prior idle completion");
    scheduler.collectAfterIdle();
}

void testInvalidIdentities()
{
    rejects<std::invalid_argument>([] { FrameScheduler invalid(0); }, "zero-slot scheduler accepted");
    FrameScheduler scheduler(1), otherSession(1);
    require(scheduler.isComplete({}), "empty completion point should be complete");
    rejects<std::out_of_range>([&] { scheduler.beginReusableSlot(1); }, "invalid slot accepted");
    rejects<std::out_of_range>([&] { (void)scheduler.completion(1); }, "invalid completion slot accepted");
    const auto future = scheduler.candidate();
    rejects<std::invalid_argument>([&] { (void)scheduler.isComplete(future); },
        "unsubmitted candidate reported as a real completion");
    scheduler.beginReusableSlot(0);
    scheduler.commitSuccess(0, future);
    rejects<std::invalid_argument>([&] { (void)otherSession.isComplete(future); },
        "foreign-session completion was accepted");
    scheduler.collectAfterIdle();
    rejects<std::invalid_argument>([&] { (void)otherSession.isComplete(future); },
        "foreign-session completion became valid after idle");
}
} // namespace

static_assert(noexcept(std::declval<FrameScheduler&>().commitSuccess(0, CompletionPoint{})));
static_assert(noexcept(std::declval<FrameScheduler&>().collectAfterIdle()));

int main()
{
    try
    {
        testFailedAttemptsKeepPending();
        testPendingBelongsToNewSubmission();
        testIdleAndOrderedCompletion();
        testInvalidIdentities();
        std::cout << "Frame scheduler tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
