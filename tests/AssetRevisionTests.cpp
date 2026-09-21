#include "render/SceneResourcePreparation.hpp"
#include "render/ResourcePreparationSource.hpp"
#include "asset/AssetManager.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
using namespace rubia::asset;

void require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

template <typename Exception, typename Function> void rejects(Function&& function)
{
    try
    {
        function();
    }
    catch (const Exception&)
    {
        return;
    }
    throw std::runtime_error("expected asset operation rejection");
}

void testIdentityAndContent()
{
    static_assert(sizeof(TextureAssetHandle) == 2 * sizeof(uint32_t));
    static_assert(!std::is_convertible_v<AssetVersion<TextureAsset>, AssetVersion<MeshAsset>>);
    AssetRegistry<std::string> assets;
    const auto handle = assets.emplace("first");
    const auto original = assets.version(handle);
    const auto other = assets.emplace("unrelated");
    require(original.handle == handle && original.contentRevision == 1 &&
                assets.isCurrent(original),
            "new asset must start at revision 1");
    require(!AssetVersion<std::string>{} && !assets.isCurrent({handle, 0}),
            "zero revision must never identify published content");

    auto previous = assets.replace(handle, "second");
    const auto changed = assets.version(handle);
    require(previous == "first" && assets.get(handle) == "second" &&
                changed.handle == original.handle && changed.contentRevision == 2 &&
                changed != original && !assets.isCurrent(original) && assets.isCurrent(changed),
            "replacement must preserve identity and invalidate the old version");
    require(assets.contentRevision(other) == 1, "replacement changed another asset's revision");

    // Restoring old bytes is a new publication, not a rewind of version history.
    previous = assets.replace(handle, std::move(previous));
    require(previous == "second" && assets.get(handle) == "first" &&
                assets.contentRevision(handle) == 3 && !assets.isCurrent(original),
            "restoring old content must advance the revision");
    static_cast<void>(assets.replace(handle, "first"));
    require(assets.contentRevision(handle) == 4,
            "revision must count publications, not hash bytes");

    const auto latest = assets.version(handle);
    require(assets.erase(handle) && !assets.contains(handle) && !assets.isCurrent(latest),
            "erase must invalidate both identity and version");
    rejects<std::out_of_range>([&] { static_cast<void>(assets.contentRevision(handle)); });
    rejects<std::out_of_range>([&] { static_cast<void>(assets.version(handle)); });
    rejects<std::out_of_range>([&] { static_cast<void>(assets.replace(handle, "stale")); });
    const auto recycled = assets.emplace("new identity");
    require(recycled.index == handle.index && recycled.generation != handle.generation &&
                assets.contentRevision(recycled) == 1 && !assets.isCurrent(original),
            "slot reuse must start a new incarnation, not resurrect an old version");

    const auto beforeReset = assets.version(recycled);
    assets.reset();
    const auto afterReset = assets.emplace("after reset");
    require(!assets.isCurrent(beforeReset) && !assets.contains(other) &&
                assets.contentRevision(afterReset) == 1 && assets.size() == 1,
            "reset must invalidate published versions");
}

// A failed move into a slot must not publish a revision or lose the reusable slot.
struct ThrowingMoveAsset
{
    inline static bool failMove = false;
    explicit ThrowingMoveAsset(int)
    {
    }
    ThrowingMoveAsset(const ThrowingMoveAsset&) = default;
    ThrowingMoveAsset(ThrowingMoveAsset&&)
    {
        if (failMove)
        {
            throw std::runtime_error("injected asset move failure");
        }
    }
};

void testFailedPublication()
{
    AssetRegistry<ThrowingMoveAsset> assets;
    ThrowingMoveAsset::failMove = true;
    rejects<std::runtime_error>([&] { static_cast<void>(assets.emplace(0)); });
    require(assets.size() == 0 && assets.handles().empty(), "failed insert published a slot");
    ThrowingMoveAsset::failMove = false;
    const auto first = assets.emplace(1);
    require(first.index == 0 && assets.contentRevision(first) == 1,
            "failed append consumed an asset slot");
    require(assets.erase(first), "test asset erase failed");
    ThrowingMoveAsset::failMove = true;
    rejects<std::runtime_error>([&] { static_cast<void>(assets.emplace(2)); });
    ThrowingMoveAsset::failMove = false;
    const auto reused = assets.emplace(3);
    require(reused.index == first.index && reused.generation != first.generation &&
                assets.contentRevision(reused) == 1 && assets.size() == 1,
            "failed recycled-slot insert consumed the slot or advanced its content revision");
}

TextureAsset::CreateInfo textureInfo(std::byte value)
{
    TextureAsset::CreateInfo info;
    info.width = info.height = 1;
    info.format = TextureFormat::RGBA8UNorm;
    info.payload.assign(4, value);
    return info;
}

void testManagedReplacement()
{
    AssetManager assets;
    const auto handle = assets.createTexture(textureInfo(std::byte{0x10}));
    const auto original = assets.version(handle);
    auto previous = assets.replaceTexture(handle, TextureAsset(textureInfo(std::byte{0x20})));
    const auto replaced = assets.version(handle);
    require(original.handle == replaced.handle && replaced.contentRevision == 2 &&
                !assets.isCurrent(original) && assets.isCurrent(replaced) &&
                previous.payload()[0] == std::byte{0x10},
            "AssetManager did not expose the committed texture revision");

    rejects<std::invalid_argument>([&] { static_cast<void>(assets.replaceTexture(handle, {})); });
    auto stale = handle;
    ++stale.generation;
    rejects<std::out_of_range>(
        [&]
        {
            static_cast<void>(
                assets.replaceTexture(stale, TextureAsset(textureInfo(std::byte{0x30}))));
        });
    require(assets.version(handle) == replaced &&
                assets.texture(handle).payload()[0] == std::byte{0x20},
            "failed replacement changed content or revision");
    static_cast<void>(assets.replaceTexture(handle, std::move(previous)));
    require(assets.contentRevision(handle) == 3 && !assets.isCurrent(original) &&
                assets.texture(handle).payload()[0] == std::byte{0x10},
            "restoring texture content must publish a fresh revision");
    assets.reset();
    require(!assets.isCurrent(replaced), "manager reset retained an old version");
    rejects<std::out_of_range>([&] { static_cast<void>(assets.version(handle)); });
}

void testManagedGeometry()
{
    AssetManager assets;
    MeshAsset::CreateInfo meshInfo;
    meshInfo.vertices.resize(3);
    SubmeshData submesh;
    submesh.vertexCount = 3;
    meshInfo.submeshes.push_back(submesh);
    const auto mesh = assets.createMesh(std::move(meshInfo));
    ModelAsset::CreateInfo modelInfo;
    modelInfo.nodes.emplace_back();
    modelInfo.nodes[0].meshes.push_back(mesh);
    const auto model = assets.createModel(std::move(modelInfo));
    const auto meshVersion = assets.version(mesh);
    const auto modelVersion = assets.version(model);
    require(meshVersion.contentRevision == 1 && modelVersion.contentRevision == 1 &&
                assets.isCurrent(meshVersion) && assets.isCurrent(modelVersion),
            "geometry assets did not expose initial revisions");
    assets.reset();
    require(!assets.isCurrent(meshVersion) && !assets.isCurrent(modelVersion),
            "geometry versions survived reset");
}

void testImmutableSnapshots()
{
    AssetManager assets;
    TextureAsset::CreateInfo info;
    info.width = info.height = 1;
    info.format = TextureFormat::RGBA8UNorm;
    info.payload.assign(4, std::byte{0x12});
    const auto handle = assets.createTexture(info);
    const auto old = assets.snapshot(handle);
    for (int i = 0; i < 64; ++i) static_cast<void>(assets.createTexture(info));
    info.payload.assign(4, std::byte{0x34});
    static_cast<void>(assets.replaceTexture(handle, TextureAsset(info)));
    const auto current = assets.snapshot(handle);
    require(old.data->payload()[0] == std::byte{0x12} &&
                current.data->payload()[0] == std::byte{0x34} &&
                old.version.contentRevision == 1 && current.version.contentRevision == 2 &&
                old.domain == current.domain,
            "snapshot bytes/version changed with asset replacement or registry growth");
    AssetManager other;
    require(other.domain() != assets.domain(), "independent managers share an asset domain");
    AssetManager moved = std::move(assets);
    require(moved.domain() == old.domain && assets.domain() != moved.domain(),
            "move did not preserve domain identity or moved-from manager reused it");
    moved.reset();
    require(old.data->payload()[0] == std::byte{0x12}, "reset invalidated an immutable snapshot");

    MeshAsset::CreateInfo meshInfo;
    meshInfo.vertices.resize(3);
    meshInfo.submeshes.push_back({0, 3, 0, 0, {}});
    const auto mesh = moved.createMesh(meshInfo);
    const auto originalMesh = moved.snapshot(mesh);
    meshInfo.vertices[0].position.x = 3.0f;
    static_cast<void>(moved.replaceMesh(mesh, MeshAsset(meshInfo)));
    require(moved.contentRevision(mesh) == 2 && originalMesh.data->vertices()[0].position.x == 0,
            "mesh replacement failed to preserve snapshot/revision semantics");
    rejects<std::invalid_argument>([&] { static_cast<void>(moved.replaceMesh(mesh, {})); });
    require(moved.contentRevision(mesh) == 2, "failed mesh replacement changed revision");
}

void testModelExpansion()
{
    static_assert(!std::is_constructible_v<rubia::render::ResourceAssetHandle, ModelAssetHandle>);
    static_assert(!std::is_constructible_v<rubia::render::ResourceAssetSnapshot, AssetSnapshot<ModelAsset>>);
    AssetManager assets;
    MeshAsset::CreateInfo meshInfo;
    meshInfo.vertices.resize(3);
    meshInfo.submeshes.push_back({0, 3, 0, 0, {}});
    const auto mesh = assets.createMesh(meshInfo);
    ModelAsset::CreateInfo info;
    info.nodes.resize(2);
    info.nodes[0].meshes = {mesh, mesh};
    info.nodes[1].meshes = {mesh};
    const auto model = assets.createModel(info);
    const auto resources = rubia::render::collectModelMeshes(assets, {model, model});
    require(resources.size() == 1 && resources.front() == mesh, "model expansion duplicated mesh resources");
    rejects<std::invalid_argument>([&] { static_cast<void>(rubia::render::resourceSnapshot(assets.snapshot(model))); });
    info.nodes.resize(1);
    info.nodes.front().meshes.clear();
    const auto empty = assets.createModel(info);
    require(rubia::render::collectModelMeshes(assets, {empty}).empty(), "hierarchy-only model created a GPU resource");
    rejects<std::out_of_range>([&] { static_cast<void>(rubia::render::collectModelMeshes(assets, {ModelAssetHandle{}})); });
}

// Exercise every public overload even for asset types with no replacement API yet.
template <typename Asset> void testInvalidManagedVersion(const AssetManager& assets)
{
    const AssetHandle<Asset> invalid;
    rejects<std::out_of_range>([&] { static_cast<void>(assets.contentRevision(invalid)); });
    rejects<std::out_of_range>([&] { static_cast<void>(assets.version(invalid)); });
    require(!assets.isCurrent(AssetVersion<Asset>{invalid, 1}), "invalid version reported current");
}
} // namespace

int main()
{
    try
    {
        testIdentityAndContent();
        testFailedPublication();
        testManagedReplacement();
        testManagedGeometry();
        testImmutableSnapshots();
        testModelExpansion();
        const AssetManager assets;
        testInvalidManagedVersion<TextureAsset>(assets);
        testInvalidManagedVersion<MaterialTemplateAsset>(assets);
        testInvalidManagedVersion<MaterialAsset>(assets);
        testInvalidManagedVersion<MeshAsset>(assets);
        testInvalidManagedVersion<ShaderAsset>(assets);
        testInvalidManagedVersion<ShaderProgramAsset>(assets);
        testInvalidManagedVersion<ModelAsset>(assets);
        std::cout << "Asset content revision tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
