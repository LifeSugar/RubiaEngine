#pragma once
#include "ApplicationGui.hpp"
#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace rubia::editor
{
struct GalleryContent
{
    static constexpr size_t materialCount = 11;
    std::array<asset::MaterialAssetHandle, materialCount> materials;
    std::array<asset::ModelAssetHandle, materialCount> models;
    std::map<std::string, asset::TextureAssetHandle> textures;
    std::vector<render::ResourceAssetHandle> roots;
};
// The interactive gallery and regression checks import exactly the same scene.
GalleryContent loadGalleryContent(const ApplicationGuiContext& context,
    const std::filesystem::path& folder, const std::function<void()>& pump);
void populateGalleryScene(scene::Scene& scene, const GalleryContent& content);
}
