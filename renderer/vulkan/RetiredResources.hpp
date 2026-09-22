#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace rubia::rhi::vulkan
{

/// Owns replaced resources without deciding when GPU use has finished.
/// Renderer keeps an unsubmitted batch, then swaps it into a reused frame
/// slot only after its new submission succeeds. Clear only after that submission
/// completes (or after device idle). This container never waits on the GPU.
class RetiredResources final
{
public:
    RetiredResources() = default;
    RetiredResources(const RetiredResources&) = delete;
    RetiredResources& operator=(const RetiredResources&) = delete;
    RetiredResources(RetiredResources&&) noexcept = default;
    RetiredResources& operator=(RetiredResources&&) noexcept = default;

    /// Allocation failure leaves the source resource and this batch unchanged.
    /// Returned owner is address-stable until reclamation, allowing no-throw
    /// exchange with live resources after retirement storage has been allocated.
    template <typename T> std::decay_t<T>& retire(T&& resource)
    {
        using Value = std::decay_t<T>;
        static_assert(!std::is_lvalue_reference_v<T>, "retirement transfers ownership");
        static_assert(std::is_nothrow_move_constructible_v<Value>);
        static_assert(std::is_nothrow_destructible_v<Value>);
        // Allocate both the vector slot and holder before moving a live owner.
        resources_.emplace_back();
        try
        {
            resources_.back() = std::make_unique<OwnedResource<Value>>(std::move(resource));
        }
        catch (...)
        {
            resources_.pop_back();
            throw;
        }
        return static_cast<OwnedResource<Value>&>(*resources_.back()).value;
    }

    void swap(RetiredResources& other) noexcept { resources_.swap(other.resources_); }
    void clear() noexcept { resources_.clear(); }
    [[nodiscard]] bool empty() const noexcept { return resources_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return resources_.size(); }

private:
    struct Resource
    {
        virtual ~Resource() = default;
    };
    template <typename T> struct OwnedResource final : Resource
    {
        explicit OwnedResource(T&& resource) noexcept : value(std::move(resource)) {}
        T value;
    };
    std::vector<std::unique_ptr<Resource>> resources_;
};

} // namespace rubia::rhi::vulkan
