#include "asset/AssetManager.hpp"
#include <functional>
#include <map>
#include <tuple>
#include <type_traits>

namespace rubia::asset
{
AnyAssetSnapshot AssetManager::captureSnapshot(AnyAssetHandle root) const
{
    using Key = std::tuple<size_t, uint32_t, uint32_t>;
    std::map<Key, AnyAssetSnapshot> captured;
    const auto owner = domain();
    std::function<AnyAssetSnapshot(AnyAssetHandle)> capture;
    capture = [&](AnyAssetHandle handle) -> AnyAssetSnapshot
    {
        const auto key =
            std::visit([&](auto h) { return Key{handle.index(), h.index, h.generation}; }, handle);
        if (const auto found = captured.find(key); found != captured.end())
        {
            return found->second;
        }
        auto result = std::visit(
            [&](auto h) -> AnyAssetSnapshot
            {
                using Handle = decltype(h);
                auto deps = std::make_shared<AssetSnapshotDependencies>();
                const auto add = [&](auto dependency)
                {
                    const AnyAssetHandle value = dependency;
                    for (const auto& existing : deps->direct)
                    {
                        if (std::visit([&](const auto& s)
                                       { return AnyAssetHandle{s.version.handle} == value; },
                                       existing))
                        {
                            return;
                        }
                    }
                    deps->direct.push_back(capture(value));
                };
                if constexpr (std::is_same_v<Handle, TextureAssetHandle>)
                {
                    return AssetSnapshot<TextureAsset>{
                        owner, version(h), std::make_shared<const TextureAsset>(texture(h)), deps};
                }
                else if constexpr (std::is_same_v<Handle, ShaderAssetHandle>)
                {
                    return AssetSnapshot<ShaderAsset>{
                        owner, version(h), std::make_shared<const ShaderAsset>(shader(h)), deps};
                }
                else if constexpr (std::is_same_v<Handle, ShaderProgramAssetHandle>)
                {
                    const auto& value = shaderProgram(h);
                    for (auto child : value.shaders())
                    {
                        add(child);
                    }
                    return AssetSnapshot<ShaderProgramAsset>{
                        owner, version(h), std::make_shared<const ShaderProgramAsset>(value), deps};
                }
                else if constexpr (std::is_same_v<Handle, MaterialTemplateAssetHandle>)
                {
                    const auto& value = materialTemplate(h);
                    if (!isMaterialTemplateCurrent(h))
                    {
                        throw std::invalid_argument("cannot snapshot a stale material template");
                    }
                    add(value.program());
                    return AssetSnapshot<MaterialTemplateAsset>{
                        owner, version(h), std::make_shared<const MaterialTemplateAsset>(value),
                        deps};
                }
                else if constexpr (std::is_same_v<Handle, MaterialAssetHandle>)
                {
                    const auto& value = material(h);
                    add(value.materialTemplate());
                    for (auto child : value.textures())
                    {
                        add(child);
                    }
                    MaterialAsset::CompiledCreateInfo info{value.name(), value.materialTemplate(),
                                                           value.renderState(),
                                                           value.parameterData(), value.textures()};
                    auto copy =
                        std::shared_ptr<const MaterialAsset>(new MaterialAsset(std::move(info)));
                    return AssetSnapshot<MaterialAsset>{owner, version(h), std::move(copy), deps};
                }
                else if constexpr (std::is_same_v<Handle, MeshAssetHandle>)
                {
                    const auto& value = mesh(h);
                    for (const auto& submesh : value.submeshes())
                    {
                        if (submesh.material)
                        {
                            add(submesh.material);
                        }
                    }
                    return AssetSnapshot<MeshAsset>{owner, version(h),
                                                    std::make_shared<const MeshAsset>(value), deps};
                }
                else
                {
                    const auto& value = model(h);
                    for (const auto& node : value.nodes())
                    {
                        for (auto child : node.meshes)
                        {
                            add(child);
                        }
                    }
                    return AssetSnapshot<ModelAsset>{
                        owner, version(h), std::make_shared<const ModelAsset>(value), deps};
                }
            },
            handle);
        captured.emplace(key, result);
        return result;
    };
    return capture(std::move(root));
}
AssetSnapshot<TextureAsset> AssetManager::snapshot(TextureAssetHandle handle) const
{
    return std::get<AssetSnapshot<TextureAsset>>(captureSnapshot(handle));
}
AssetSnapshot<MeshAsset> AssetManager::snapshot(MeshAssetHandle handle) const
{
    return std::get<AssetSnapshot<MeshAsset>>(captureSnapshot(handle));
}
AssetSnapshot<ShaderAsset> AssetManager::snapshot(ShaderAssetHandle handle) const
{
    return std::get<AssetSnapshot<ShaderAsset>>(captureSnapshot(handle));
}
AssetSnapshot<ShaderProgramAsset> AssetManager::snapshot(ShaderProgramAssetHandle handle) const
{
    return std::get<AssetSnapshot<ShaderProgramAsset>>(captureSnapshot(handle));
}
AssetSnapshot<MaterialTemplateAsset> AssetManager::snapshot(
    MaterialTemplateAssetHandle handle) const
{
    return std::get<AssetSnapshot<MaterialTemplateAsset>>(captureSnapshot(handle));
}
AssetSnapshot<MaterialAsset> AssetManager::snapshot(MaterialAssetHandle handle) const
{
    return std::get<AssetSnapshot<MaterialAsset>>(captureSnapshot(handle));
}
AssetSnapshot<ModelAsset> AssetManager::snapshot(ModelAssetHandle handle) const
{
    return std::get<AssetSnapshot<ModelAsset>>(captureSnapshot(handle));
}
} // namespace rubia::asset
