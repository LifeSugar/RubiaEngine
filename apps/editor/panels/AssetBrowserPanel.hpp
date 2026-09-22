#pragma once

#include "EditorFwd.hpp"
#include "panels/MaterialAuthoringPanel.hpp"
#include "EditorSelection.hpp"
#include "texture/TextureImportRegistry.hpp"

#include <vector>
#include <array>

namespace rubia::editor
{

/// Lists the runtime asset registry and emits editor import operations.
class AssetBrowserPanel final
{
public:
    [[nodiscard]] std::vector<importer::texture::TextureReimportRequest> draw(
        const asset::AssetManager& assets,
        const importer::texture::TextureImportRegistry* textureImports,
        EditorSelection& selection,
        bool* open = nullptr, EditorAssetController* controller = nullptr);

private:
    void drawFiles(const asset::AssetManager&, EditorAssetController&);
    MaterialAuthoringPanel materialAuthoring_;
    std::array<char, 2048> directory_{};
    std::array<char, 128> entryPoint_{'m','a','i','n','\0'};
    int shaderStage_ = 0;
    bool srgb_ = true;
    std::array<char, 128> search_{};
};

} // namespace rubia::editor
