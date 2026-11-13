#pragma once

#include <cstdint>
#include <limits>

namespace rubia::asset
{

inline constexpr uint32_t kInvalidAssetIndex =
    std::numeric_limits<uint32_t>::max();

/// Type-safe, generation-checked identity within one AssetManager.
/// Content replacement preserves this identity; use AssetVersion to capture content freshness.
template <typename Asset>
struct AssetHandle
{
    uint32_t index = kInvalidAssetIndex;
    uint32_t generation = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return index != kInvalidAssetIndex;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return valid();
    }
};

template <typename Asset>
[[nodiscard]] bool operator==(
    AssetHandle<Asset> lhs,
    AssetHandle<Asset> rhs) noexcept
{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

template <typename Asset>
[[nodiscard]] bool operator!=(
    AssetHandle<Asset> lhs,
    AssetHandle<Asset> rhs) noexcept
{
    return !(lhs == rhs);
}

// Zero denotes no published content. Revisions are local to one asset incarnation.
using AssetContentRevision = uint64_t;
inline constexpr AssetContentRevision kInvalidAssetContentRevision = 0;

/// A metadata snapshot, not ownership of the asset's bytes. Only compare snapshots
/// from the same AssetManager/registry; dependency revisions are tracked separately.
template <typename Asset> struct AssetVersion
{
    AssetHandle<Asset> handle;
    AssetContentRevision contentRevision = kInvalidAssetContentRevision;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(handle) && contentRevision != kInvalidAssetContentRevision;
    }
};

template <typename Asset>
[[nodiscard]] bool operator==(AssetVersion<Asset> lhs, AssetVersion<Asset> rhs) noexcept
{
    return lhs.handle == rhs.handle && lhs.contentRevision == rhs.contentRevision;
}

template <typename Asset>
[[nodiscard]] bool operator!=(AssetVersion<Asset> lhs, AssetVersion<Asset> rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace rubia::asset
