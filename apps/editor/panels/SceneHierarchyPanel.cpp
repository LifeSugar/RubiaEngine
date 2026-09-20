#include "panels/SceneHierarchyPanel.hpp"

#include "asset/AssetManager.hpp"
#include "asset/ModelAsset.hpp"
#include "EditorSelection.hpp"
#include "scene/Scene.hpp"

#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

namespace rubia::editor
{

namespace
{

struct ModelHierarchy final
{
    std::vector<uint32_t> roots;
    std::vector<std::vector<uint32_t>> children;
};

[[nodiscard]] ModelHierarchy buildModelHierarchy(const asset::ModelAsset& model)
{
    const std::vector<asset::ModelNode>& nodes = model.nodes();
    ModelHierarchy hierarchy{};
    hierarchy.children.resize(nodes.size());
    hierarchy.roots.reserve(nodes.size());

    for (uint32_t index = 0;
         index < static_cast<uint32_t>(nodes.size());
         ++index)
    {
        const uint32_t parent = nodes[index].parent;
        if (parent == asset::kInvalidModelNodeIndex)
        {
            hierarchy.roots.push_back(index);
        }
        else if (parent < nodes.size())
        {
            hierarchy.children[parent].push_back(index);
        }
    }
    return hierarchy;
}

void drawMeshHierarchy(
    const asset::AssetManager& assets,
    asset::MeshAssetHandle meshHandle,
    EditorSelection& selection)
{
    if (!assets.contains(meshHandle))
    {
        ImGui::TextDisabled("Missing MeshAsset");
        return;
    }

    const asset::MeshAsset& mesh = assets.mesh(meshHandle);
    const bool hasSubmeshes = !mesh.submeshes().empty();
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;
    const asset::MeshAssetHandle* selectedMesh =
        std::get_if<asset::MeshAssetHandle>(&selection.target());
    if (selectedMesh != nullptr && *selectedMesh == meshHandle)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (!hasSubmeshes)
    {
        flags |= ImGuiTreeNodeFlags_Leaf |
            ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    const char* meshLabel = mesh.name().empty()
        ? "Unnamed Mesh"
        : mesh.name().c_str();
    const bool meshOpen = ImGui::TreeNodeEx(meshLabel, flags);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        selection.select(meshHandle);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("MeshAsset (read-only)");
    }

    if (meshOpen && hasSubmeshes)
    {
        for (uint32_t submeshIndex = 0;
             submeshIndex <
                 static_cast<uint32_t>(mesh.submeshes().size());
             ++submeshIndex)
        {
            ImGui::PushID(static_cast<int>(submeshIndex));
            ImGuiTreeNodeFlags submeshFlags =
                ImGuiTreeNodeFlags_Leaf |
                ImGuiTreeNodeFlags_NoTreePushOnOpen |
                ImGuiTreeNodeFlags_SpanAvailWidth;
            const SubmeshTarget* selectedSubmesh =
                std::get_if<SubmeshTarget>(&selection.target());
            if (selectedSubmesh != nullptr &&
                selectedSubmesh->mesh == meshHandle &&
                selectedSubmesh->submeshIndex == submeshIndex)
            {
                submeshFlags |= ImGuiTreeNodeFlags_Selected;
            }

            const std::string label =
                "Submesh " + std::to_string(submeshIndex);
            ImGui::TreeNodeEx(label.c_str(), submeshFlags);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                selection.select(SubmeshTarget{
                    meshHandle,
                    submeshIndex
                });
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Submesh (read-only)");
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}

void drawModelHierarchy(
    const asset::AssetManager& assets,
    const asset::ModelAsset& model,
    asset::ModelAssetHandle modelHandle,
    uint32_t sceneNodeIndex,
    EditorSelection& selection)
{
    const std::vector<asset::ModelNode>& nodes = model.nodes();
    if (nodes.empty())
    {
        return;
    }

    const ModelHierarchy hierarchy = buildModelHierarchy(model);
    ImGui::PushID("ModelAssetHierarchy");
    ImGui::PushID(static_cast<int>(sceneNodeIndex));
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

    std::function<void(uint32_t)> drawModelNode =
        [&](uint32_t nodeIndex)
        {
            const asset::ModelNode& node = nodes[nodeIndex];
            const bool hasChildren =
                !hierarchy.children[nodeIndex].empty() ||
                !node.meshes.empty();

            ImGuiTreeNodeFlags flags =
                ImGuiTreeNodeFlags_OpenOnArrow |
                ImGuiTreeNodeFlags_SpanAvailWidth;
            const ModelNodeTarget* selected =
                std::get_if<ModelNodeTarget>(&selection.target());
            if (selected != nullptr &&
                selected->model == modelHandle &&
                selected->nodeIndex == nodeIndex &&
                selected->sceneNodeIndex == sceneNodeIndex)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (!hasChildren)
            {
                flags |= ImGuiTreeNodeFlags_Leaf |
                    ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }

            ImGui::PushID(static_cast<int>(nodeIndex));
            const char* label = node.name.empty()
                ? "Unnamed ModelNode"
                : node.name.c_str();
            const bool nodeOpen = ImGui::TreeNodeEx(label, flags);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                selection.select(ModelNodeTarget{
                    modelHandle,
                    nodeIndex,
                    sceneNodeIndex
                });
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("ModelNode (read-only)");
            }
            if (nodeOpen && hasChildren)
            {
                for (uint32_t childIndex : hierarchy.children[nodeIndex])
                {
                    drawModelNode(childIndex);
                }
                ImGui::PushID("MeshReferences");
                for (std::size_t meshIndex = 0;
                     meshIndex < node.meshes.size();
                     ++meshIndex)
                {
                    ImGui::PushID(static_cast<int>(meshIndex));
                    drawMeshHierarchy(
                        assets,
                        node.meshes[meshIndex],
                        selection);
                    ImGui::PopID();
                }
                ImGui::PopID();
                ImGui::TreePop();
            }
            ImGui::PopID();
        };

    for (uint32_t rootIndex : hierarchy.roots)
    {
        drawModelNode(rootIndex);
    }
    ImGui::PopStyleColor();

    ImGui::PopID();
    ImGui::PopID();
}

} // namespace

void SceneHierarchyPanel::draw(
    const scene::Scene& scene,
    const asset::AssetManager& assets,
    EditorSelection& selection,
    bool* open)
{
    const bool visible = ImGui::Begin("Scene Hierarchy", open);
    if (!visible)
    {
        ImGui::End();
        return;
    }

    const std::vector<scene::SceneNode>& nodes = scene.nodes();
    ImGui::TextDisabled("%zu objects", nodes.size());
    ImGui::Separator();
    if (const SceneNodeTarget* target =
            std::get_if<SceneNodeTarget>(&selection.target());
        target != nullptr && target->nodeIndex >= nodes.size())
    {
        selection.clear();
    }
    else if (const ModelNodeTarget* target =
                 std::get_if<ModelNodeTarget>(&selection.target());
             target != nullptr &&
             (!assets.contains(target->model) ||
              target->nodeIndex >=
                  assets.model(target->model).nodes().size() ||
              (target->sceneNodeIndex &&
               *target->sceneNodeIndex >= nodes.size())))
    {
        selection.clear();
    }
    else if (const asset::MeshAssetHandle* target =
                 std::get_if<asset::MeshAssetHandle>(&selection.target());
             target != nullptr && !assets.contains(*target))
    {
        selection.clear();
    }
    else if (const SubmeshTarget* target =
                 std::get_if<SubmeshTarget>(&selection.target());
             target != nullptr &&
             (!assets.contains(target->mesh) ||
              target->submeshIndex >=
                  assets.mesh(target->mesh).submeshes().size()))
    {
        selection.clear();
    }

    std::vector<uint32_t> roots;
    std::vector<std::vector<uint32_t>> children(nodes.size());
    roots.reserve(nodes.size());
    for (uint32_t index = 0;
         index < static_cast<uint32_t>(nodes.size());
         ++index)
    {
        const uint32_t parent = nodes[index].parent;
        if (parent == scene::kInvalidSceneNodeIndex)
        {
            roots.push_back(index);
        }
        else
        {
            children[parent].push_back(index);
        }
    }

    const std::string sceneLabel = scene.name().empty()
        ? "Untitled Scene"
        : scene.name();
    const ImGuiTreeNodeFlags sceneFlags =
        ImGuiTreeNodeFlags_DefaultOpen |
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;
    const bool sceneOpen = ImGui::TreeNodeEx(
        "##SceneRoot",
        sceneFlags,
        "%s",
        sceneLabel.c_str());

    if (sceneOpen)
    {
        if (nodes.empty())
        {
            ImGui::TextDisabled("No objects in this scene");
        }
        std::function<void(uint32_t)> drawNode =
            [&](uint32_t nodeIndex)
            {
                const scene::SceneNode& node = nodes[nodeIndex];
                const asset::ModelAsset* model = nullptr;
                if (node.model && assets.contains(node.model))
                {
                    model = &assets.model(node.model);
                }
                const bool hasModelHierarchy =
                    model != nullptr && !model->nodes().empty();
                const bool hasChildren =
                    !children[nodeIndex].empty() || hasModelHierarchy;

                ImGuiTreeNodeFlags flags =
                    ImGuiTreeNodeFlags_OpenOnArrow |
                    ImGuiTreeNodeFlags_SpanAvailWidth;
                const SceneNodeTarget* selected =
                    std::get_if<SceneNodeTarget>(&selection.target());
                if (selected != nullptr &&
                    selected->nodeIndex == nodeIndex)
                {
                    flags |= ImGuiTreeNodeFlags_Selected;
                }
                if (!hasChildren)
                {
                    flags |= ImGuiTreeNodeFlags_Leaf |
                        ImGuiTreeNodeFlags_NoTreePushOnOpen;
                }
                else if (hasModelHierarchy)
                {
                    // Make the imported hierarchy visible on first launch.
                    flags |= ImGuiTreeNodeFlags_DefaultOpen;
                }

                ImGui::PushID(static_cast<int>(nodeIndex));
                const char* label = node.name.empty()
                    ? "Unnamed SceneNode"
                    : node.name.c_str();
                const bool nodeOpen = ImGui::TreeNodeEx(label, flags);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    selection.select(SceneNodeTarget{nodeIndex});
                }

                if (nodeOpen && hasChildren)
                {
                    for (uint32_t childIndex : children[nodeIndex])
                    {
                        drawNode(childIndex);
                    }
                    if (model != nullptr)
                    {
                        drawModelHierarchy(
                            assets,
                            *model,
                            node.model,
                            nodeIndex,
                            selection);
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            };

        for (uint32_t rootIndex : roots)
        {
            drawNode(rootIndex);
        }
        ImGui::TreePop();
    }

    if (ImGui::IsWindowHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered())
    {
        selection.clear();
    }
    ImGui::End();
}

} // namespace rubia::editor
