#pragma once
#include "asset/AssetFwd.hpp"
#include <memory>
#include <utility>
#include <vector>

namespace rubia::asset
{
// An identity token, retained by snapshots/caches so addresses cannot be recycled.
struct AssetDomainTag final
{
};
using AssetDomainId = std::shared_ptr<const AssetDomainTag>;

// Moving a manager preserves its domain. A reused moved-from manager gets a fresh token.
class AssetDomain final
{
public:
    AssetDomain() = default;
    AssetDomain(const AssetDomain&) = delete;
    AssetDomain& operator=(const AssetDomain&) = delete;
    AssetDomain(AssetDomain&&) noexcept = default;
    AssetDomain& operator=(AssetDomain&&) noexcept = default;
    AssetDomainId id() const
    {
        if (!id_)
        {
            id_ = std::make_shared<AssetDomainTag>();
        }
        return id_;
    }

private:
    mutable AssetDomainId id_ = std::make_shared<AssetDomainTag>();
};

struct AssetSnapshotDependencies;

// Owns immutable bytes for precisely one version, independent of registry relocation/replacement.
// Capture on the asset owner's thread. A snapshot does not make AssetManager thread-safe.
template <typename Asset> struct AssetSnapshot
{
    AssetDomainId domain;
    AssetVersion<Asset> version;
    std::shared_ptr<const Asset> data;
    std::shared_ptr<const AssetSnapshotDependencies> dependencies;
    explicit operator bool() const noexcept
    {
        return domain && version && data && bool(*data);
    }
};
using AnyAssetSnapshot =
    std::variant<AssetSnapshot<TextureAsset>, AssetSnapshot<MeshAsset>, AssetSnapshot<ShaderAsset>,
                 AssetSnapshot<ShaderProgramAsset>, AssetSnapshot<MaterialTemplateAsset>,
                 AssetSnapshot<MaterialAsset>, AssetSnapshot<ModelAsset>>;
struct AssetSnapshotDependencies
{
    std::vector<AnyAssetSnapshot> direct;
};
} // namespace rubia::asset
