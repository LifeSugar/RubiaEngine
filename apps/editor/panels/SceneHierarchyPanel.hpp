#pragma once

#include "EditorFwd.hpp"

namespace rubia::editor
{
class EditorSelection;
class EditorAssetController;

/// Draws the SceneNode hierarchy without depending on Renderer state.
/// Referenced ModelAsset nodes are projected as a read-only subtree.
class SceneHierarchyPanel final
{
public:
    void draw(
        const scene::Scene& scene,
        const asset::AssetManager& assets,
        EditorSelection& selection,
        bool* open = nullptr, EditorAssetController* controller = nullptr);
};

} // namespace rubia::editor
