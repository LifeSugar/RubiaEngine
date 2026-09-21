#pragma once

#include "asset/AssetHandle.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace rubia::asset
{

/// Stores one asset type and rejects handles whose slots were recycled.
template <typename Asset>
class AssetRegistry final
{
public:
    using Handle = AssetHandle<Asset>;

    [[nodiscard]] Handle insert(Asset asset)
    {
        uint32_t index = 0;
        const bool append = freeIndices_.empty();
        if (append)
        {
            if (slots_.size() >= kInvalidAssetIndex)
            {
                throw std::overflow_error("asset registry capacity exceeded");
            }
            index = static_cast<uint32_t>(slots_.size());
            slots_.push_back({});
        }
        else
        {
            index = freeIndices_.back();
        }

        Slot& slot = slots_[index];
        try
        {
            slot.asset.emplace(std::move(asset));
        }
        catch (...)
        {
            if (append)
            {
                slots_.pop_back();
            }
            throw;
        }
        if (!append)
        {
            freeIndices_.pop_back();
        }
        slot.contentRevision = 1;
        slot.lastIssuedRevision = 1;
        ++size_;
        return {index, slot.generation};
    }

    template <typename... Arguments>
    [[nodiscard]] Handle emplace(Arguments&&... arguments)
    {
        return insert(Asset(std::forward<Arguments>(arguments)...));
    }

    [[nodiscard]] bool contains(Handle handle) const noexcept
    {
        return handle.index < slots_.size() &&
            slots_[handle.index].asset.has_value() &&
            slots_[handle.index].generation == handle.generation;
    }

    [[nodiscard]] const Asset& get(Handle handle) const
    {
        if (!contains(handle))
        {
            throw std::out_of_range("asset handle is invalid or stale");
        }
        return *slots_[handle.index].asset;
    }

    [[nodiscard]] AssetContentRevision contentRevision(Handle handle) const
    {
        if (!contains(handle))
        {
            throw std::out_of_range("asset handle is invalid or stale");
        }
        return slots_[handle.index].contentRevision;
    }

    [[nodiscard]] AssetVersion<Asset> version(Handle handle) const
    {
        return {handle, contentRevision(handle)};
    }

    [[nodiscard]] bool isCurrent(AssetVersion<Asset> version) const noexcept
    {
        return contains(version.handle) &&
               slots_[version.handle.index].contentRevision == version.contentRevision;
    }

    /// Replaces an asset without changing its handle generation. The caller
    /// constructs and validates the candidate before entering this method.
    /// Each successful replacement advances the revision, including restoring old content.
    [[nodiscard]] Asset replace(Handle handle, Asset replacement)
    {
        // Commit and return must not throw after the old content has been changed.
        static_assert(std::is_nothrow_move_constructible_v<Asset> &&
                          std::is_nothrow_swappable_v<Asset>,
                      "asset replacement requires nonthrowing move construction and swap");
        if (!contains(handle))
        {
            throw std::out_of_range("asset handle is invalid or stale");
        }

        Slot& slot = slots_[handle.index];
        if (slot.lastIssuedRevision == std::numeric_limits<AssetContentRevision>::max())
        {
            throw std::overflow_error("asset content revision exhausted");
        }
        std::optional<Asset> candidate(std::move(replacement));
        slot.asset.swap(candidate);
        slot.contentRevision = ++slot.lastIssuedRevision;
        return std::move(*candidate);
    }

    // Reserve a unique identity for unpublished content. Abandoned reservations
    // leave gaps; subsequent edits must never reuse a candidate's revision.
    [[nodiscard]] AssetVersion<Asset> reserveVersion(Handle handle)
    {
        static_cast<void>(get(handle));
        auto& slot = slots_[handle.index];
        if (slot.lastIssuedRevision == std::numeric_limits<AssetContentRevision>::max())
            throw std::overflow_error("asset content revision exhausted");
        return {handle, ++slot.lastIssuedRevision};
    }
    // Caller validates expected immediately before a non-interleaved commit.
    void commitReserved(AssetVersion<Asset> expected, AssetVersion<Asset> reserved,
                        Asset& candidate) noexcept
    {
        static_assert(std::is_nothrow_swappable_v<Asset>);
        assert(isCurrent(expected) && expected.handle == reserved.handle &&
               reserved.contentRevision > expected.contentRevision &&
               reserved.contentRevision <= slots_[expected.handle.index].lastIssuedRevision);
        auto& slot = slots_[expected.handle.index];
        using std::swap;
        swap(*slot.asset, candidate);
        slot.contentRevision = reserved.contentRevision;
    }

    bool erase(Handle handle) noexcept
    {
        if (!contains(handle))
        {
            return false;
        }

        Slot& slot = slots_[handle.index];
        slot.asset.reset();
        slot.contentRevision = kInvalidAssetContentRevision;
        slot.generation = nextGeneration(slot.generation);
        freeIndices_.push_back(handle.index);
        --size_;
        return true;
    }

    void reset() noexcept
    {
        freeIndices_.clear();
        freeIndices_.reserve(slots_.size());
        for (uint32_t index = 0;
             index < static_cast<uint32_t>(slots_.size());
             ++index)
        {
            Slot& slot = slots_[index];
            slot.asset.reset();
            slot.contentRevision = kInvalidAssetContentRevision;
            slot.generation = nextGeneration(slot.generation);
            freeIndices_.push_back(index);
        }
        size_ = 0;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    [[nodiscard]] std::vector<Handle> handles() const
    {
        std::vector<Handle> result;
        result.reserve(size_);
        for (uint32_t index = 0;
             index < static_cast<uint32_t>(slots_.size());
             ++index)
        {
            const Slot& slot = slots_[index];
            if (slot.asset.has_value())
            {
                result.push_back({index, slot.generation});
            }
        }
        return result;
    }

private:
    struct Slot
    {
        std::optional<Asset> asset;
        uint32_t generation = 1;
        AssetContentRevision contentRevision = kInvalidAssetContentRevision;
        AssetContentRevision lastIssuedRevision = kInvalidAssetContentRevision;
    };

    [[nodiscard]] static uint32_t nextGeneration(uint32_t generation) noexcept
    {
        return generation == std::numeric_limits<uint32_t>::max()
            ? 1
            : generation + 1;
    }

    std::vector<Slot> slots_;
    std::vector<uint32_t> freeIndices_;
    std::size_t size_ = 0;
};

} // namespace rubia::asset
