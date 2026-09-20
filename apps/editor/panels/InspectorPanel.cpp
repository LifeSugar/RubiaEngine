#include "panels/InspectorPanel.hpp"

#include "asset/AssetManager.hpp"
#include "render/ApplicationGuiRenderBridge.hpp"
#include "scene/Scene.hpp"

#include <imgui.h>

#include <iterator>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace rubia::editor
{

    namespace
    {

        template <typename... Visitors>
        struct Overloaded : Visitors...
        {
            using Visitors::operator()...;
        };

        template <typename... Visitors>
        Overloaded(Visitors...) -> Overloaded<Visitors...>;

    } // namespace

    std::vector<importer::texture::TextureReimportRequest> InspectorPanel::draw(
        const scene::Scene &scene,
        const asset::AssetManager &assets,
        render::ApplicationGuiRenderBridge &texturePreviews,
        const importer::texture::TextureImportRegistry* textureImports,
        EditorSelection &selection,
        bool *open)
    {
        const bool visible = ImGui::Begin("Inspector", open);
        if (!visible)
        {
            ImGui::End();
            return {};
        }

        if (selection.canNavigateBack() && ImGui::SmallButton("< Back"))
        {
            selection.navigateBack();
        }

        std::vector<importer::texture::TextureReimportRequest> textureReimports;
        const std::optional<InspectorTarget> navigation = std::visit(
            Overloaded{
                [](std::monostate) -> std::optional<InspectorTarget>
                {
                    ImGui::Spacing();
                    ImGui::SeparatorText("Nothing selected");
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped("Select an object in the scene hierarchy or an asset below to view its properties.");
                    ImGui::PopStyleColor();
                    return std::nullopt;
                },
                [&](SceneNodeTarget target)
                {
                    return sceneNodeInspector_.draw(scene, assets, target);
                },
                [&](asset::ModelAssetHandle target)
                {
                    return modelInspector_.drawModelAsset(assets, target);
                },
                [&](ModelNodeTarget target)
                {
                    return modelInspector_.drawModelNode(scene, assets, target);
                },
                [&](asset::MeshAssetHandle target)
                {
                    return modelInspector_.drawMeshAsset(assets, target);
                },
                [&](SubmeshTarget target)
                {
                    return modelInspector_.drawSubmesh(assets, target);
                },
                [&](asset::MaterialAssetHandle target)
                {
                    MaterialInspectorOutput output =
                        materialInspector_.drawMaterialAsset(
                        assets,
                        texturePreviews,
                        textureImports,
                        target);
                    textureReimports.insert(
                        textureReimports.end(),
                        std::make_move_iterator(
                            output.textureReimports.begin()),
                        std::make_move_iterator(
                            output.textureReimports.end()));
                    return output.navigation;
                },
                [&](asset::MaterialTemplateAssetHandle target)
                {
                    return materialInspector_.drawMaterialTemplate(
                        assets,
                        target);
                },
                [&](asset::TextureAssetHandle target)
                    -> std::optional<InspectorTarget>
                {
                    std::optional<importer::texture::TextureReimportRequest> request =
                        textureInspector_.draw(
                        assets,
                        texturePreviews,
                        textureImports,
                        target);
                    if (request)
                    {
                        textureReimports.push_back(std::move(*request));
                    }
                    return std::nullopt;
                }},
            selection.target());

        if (navigation)
        {
            selection.navigateTo(std::move(*navigation));
        }
        ImGui::End();
        return textureReimports;
    }

} // namespace rubia::editor
