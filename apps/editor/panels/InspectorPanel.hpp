#pragma once

#include "EditorFwd.hpp"
#include "EditorSelection.hpp"
#include "inspectors/MaterialInspector.hpp"
#include "inspectors/ModelInspector.hpp"
#include "inspectors/SceneNodeInspector.hpp"
#include "inspectors/TextureInspector.hpp"

#include <vector>

namespace rubia::editor
{
class EditorAssetController;

/// Owns the Inspector window and routes selections to type inspectors.
class InspectorPanel final
{
public:
    [[nodiscard]] std::vector<importer::texture::TextureReimportRequest> draw(
        const scene::Scene& scene,
        const asset::AssetManager& assets,
        render::ApplicationGuiRenderBridge& texturePreviews,
        const importer::texture::TextureImportRegistry* textureImports,
        EditorSelection& selection,
        bool* open = nullptr, EditorAssetController* controller = nullptr);

private:
    SceneNodeInspector sceneNodeInspector_;
    ModelInspector modelInspector_;
    MaterialInspector materialInspector_;
    TextureInspector textureInspector_;
};

} // namespace rubia::editor
