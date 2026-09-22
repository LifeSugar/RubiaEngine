#include "panels/AssetBrowserPanel.hpp"

#include "asset/AssetManager.hpp"
#include "inspectors/InspectorWidgets.hpp"

#include <imgui.h>

#include <string>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <vector>

namespace rubia::editor
{
namespace
{

struct GlobalTextureReimportBatch
{
    std::vector<importer::texture::TextureReimportRequest> requests;
    bool busy = false;
};

[[nodiscard]] GlobalTextureReimportBatch collectTextureReimports(
    const asset::AssetManager& assets,
    const importer::texture::TextureImportRegistry* textureImports)
{
    GlobalTextureReimportBatch result{};
    if (textureImports == nullptr)
    {
        return result;
    }

    for (asset::TextureAssetHandle texture : assets.textureHandles())
    {
        const importer::texture::TextureImportRecord* record = textureImports->find(texture);
        if (record == nullptr)
        {
            continue;
        }
        result.busy = result.busy || record->reimporting;
        result.requests.push_back({texture, record->settings});
    }
    return result;
}

template <typename Handle>
[[nodiscard]] bool isSelected(
    const EditorSelection& selection,
    Handle handle) noexcept
{
    const Handle* selected = std::get_if<Handle>(&selection.target());
    return selected != nullptr && *selected == handle;
}

template <typename Handle>
void pushHandleId(Handle handle)
{
    ImGui::PushID(static_cast<int>(handle.index));
    ImGui::PushID(static_cast<int>(handle.generation));
}

void popHandleId()
{
    ImGui::PopID();
    ImGui::PopID();
}

} // namespace

std::vector<importer::texture::TextureReimportRequest> AssetBrowserPanel::draw(
    const asset::AssetManager& assets,
    const importer::texture::TextureImportRegistry* textureImports,
    EditorSelection& selection,
    bool* open, EditorAssetController* controller)
{
    std::vector<importer::texture::TextureReimportRequest> reimports;
    const bool visible = ImGui::Begin("Assets", open);
    if (!visible)
    {
        ImGui::End();
        return reimports;
    }

    if (controller) drawFiles(assets, *controller);

    const GlobalTextureReimportBatch batch =
        collectTextureReimports(assets, textureImports);
    const float searchWidth = ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 22.0f;
    ImGui::SetNextItemWidth(searchWidth > ImGui::GetFontSize() * 12.0f ? searchWidth : -1.0f);
    ImGui::InputTextWithHint("##AssetSearch", "Search assets...", search_.data(), search_.size());
    const ImGuiTextFilter filter(search_.data());
    if (searchWidth > ImGui::GetFontSize() * 12.0f)
    {
        ImGui::SameLine();
    }
    const bool reimportDisabled = batch.requests.empty() || batch.busy;
    ImGui::BeginDisabled(reimportDisabled);
    const std::string buttonLabel =
        "Reimport All (" + std::to_string(batch.requests.size()) + ")";
    if (ImGui::Button(buttonLabel.c_str()))
    {
        reimports = batch.requests;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        if (batch.requests.empty())
        {
            ImGui::SetTooltip("No source-backed textures are registered");
        }
        else if (batch.busy)
        {
            ImGui::SetTooltip("Texture reimport is already running");
        }
        else
        {
            ImGui::SetTooltip(
                "Cook and replace every registered texture sequentially");
        }
    }

    ImGui::Separator();
    const int columns = ImGui::GetContentRegionAvail().x > ImGui::GetFontSize() * 42.0f ? 3 : 1;
    if (!ImGui::BeginTable("##AssetCategories", columns, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::End();
        return reimports;
    }
    ImGui::TableNextColumn();
    const std::vector<asset::ModelAssetHandle> models = assets.modelHandles();
    const std::string modelHeader =
        "Models (" + std::to_string(models.size()) + ")";
    if (ImGui::CollapsingHeader(
            modelHeader.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("Models");
        for (asset::ModelAssetHandle handle : models)
        {
            const asset::ModelAsset& model = assets.model(handle);
            if (!filter.PassFilter(widgets::displayName(model.name(), "Unnamed Model")))
                continue;
            pushHandleId(handle);
            if (ImGui::Selectable(
                    widgets::displayName(
                        model.name(),
                        "Unnamed Model"),
                    isSelected(selection, handle)))
            {
                selection.select(InspectorTarget{handle});
            }
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(ModelPayload, &handle, sizeof(handle));
                ImGui::TextUnformatted(model.name().c_str());
                ImGui::EndDragDropSource();
            }
            popHandleId();
        }
        ImGui::PopID();
    }

    ImGui::TableNextColumn();
    const std::vector<asset::MaterialAssetHandle> materials =
        assets.materialHandles();
    const std::string materialHeader =
        "Materials (" + std::to_string(materials.size()) + ")";
    if (ImGui::CollapsingHeader(
            materialHeader.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("Materials");
        for (asset::MaterialAssetHandle handle : materials)
        {
            const asset::MaterialAsset& material = assets.material(handle);
            if (!filter.PassFilter(widgets::displayName(material.name(), "Unnamed Material")))
                continue;
            pushHandleId(handle);
            if (ImGui::Selectable(
                    widgets::displayName(
                        material.name(),
                        "Unnamed Material"),
                    isSelected(selection, handle)))
            {
                selection.select(InspectorTarget{handle});
            }
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(MaterialPayload, &handle, sizeof(handle));
                ImGui::TextUnformatted(material.name().c_str());
                ImGui::EndDragDropSource();
            }
            popHandleId();
        }
        ImGui::PopID();
    }

    ImGui::TableNextColumn();
    const std::vector<asset::TextureAssetHandle> textures =
        assets.textureHandles();
    const std::string textureHeader =
        "Textures (" + std::to_string(textures.size()) + ")";
    if (ImGui::CollapsingHeader(
            textureHeader.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("Textures");
        for (asset::TextureAssetHandle handle : textures)
        {
            const asset::TextureAsset& texture = assets.texture(handle);
            if (!filter.PassFilter(widgets::displayName(texture.name(), "Unnamed Texture")))
                continue;
            pushHandleId(handle);
            if (ImGui::Selectable(
                    widgets::displayName(
                        texture.name(),
                        "Unnamed Texture"),
                    isSelected(selection, handle)))
            {
                selection.select(InspectorTarget{handle});
            }
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(TexturePayload, &handle, sizeof(handle));
                ImGui::TextUnformatted(texture.name().c_str());
                ImGui::EndDragDropSource();
            }
            if (textureImports != nullptr)
            {
                const importer::texture::TextureImportRecord* record =
                    textureImports->find(handle);
                if (record != nullptr && ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(
                        "%s\n%s",
                        record->sourcePath.string().c_str(),
                        record->reimporting ? "Reimporting" : "Source-backed");
                }
            }
            popHandleId();
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (ImGui::CollapsingHeader("Shaders"))
        for (auto handle : assets.shaderHandles())
        {
            const auto& shader = assets.shader(handle);
            ImGui::Text("%s [%s / %s]", shader.name().c_str(),
                shader.stage() == asset::ShaderStage::Vertex ? "VS" : "PS", shader.entryPoint().c_str());
        }
    if (controller) materialAuthoring_.draw(assets, *controller);

    ImGui::End();
    return reimports;
}


void AssetBrowserPanel::drawFiles(const asset::AssetManager&, EditorAssetController& controller)
{
    ImGui::InputTextWithHint("##Directory", "Absolute directory path (any drive)", directory_.data(), directory_.size());
    if (ImGui::Button("Open Directory")) controller.browse(std::filesystem::u8path(directory_.data()));
    ImGui::SameLine();
    if (ImGui::Button("Up") && !controller.directory().empty()) controller.browse(controller.directory().parent_path());
    ImGui::SameLine();
    if (ImGui::Button("Refresh") && !controller.directory().empty()) controller.browse(controller.directory());
    ImGui::TextWrapped("%s", controller.directory().string().c_str());
    ImGui::Combo("Shader stage", &shaderStage_, "Vertex (VS)\0Pixel (PS)\0");
    ImGui::InputText("Entry point", entryPoint_.data(), entryPoint_.size());
    ImGui::Checkbox("Texture sRGB (off for normal/data maps)", &srgb_);
    ImGui::TextWrapped("Double-click to import. Drag GLB/glTF files or loaded models into Scene Hierarchy.");
    if (!controller.message().empty()) ImGui::TextWrapped("%s", controller.message().c_str());
    if (ImGui::BeginChild("##SourceFiles", ImVec2(0, 150), ImGuiChildFlags_Borders))
    {
        for (const auto& entry : controller.files())
        {
            const auto path = entry.path.u8string();
            const std::string utf8(reinterpret_cast<const char*>(path.data()), path.size());
            const auto filename = entry.path.filename().u8string();
            const std::string label = (entry.directory ? "[Folder] " : "") +
                std::string(reinterpret_cast<const char*>(filename.data()), filename.size());
            ImGui::PushID(utf8.c_str());
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0))
            {
                if (entry.directory) controller.browse(entry.path);
                else
                {
                    EditorAssetController::ImportOptions options;
                    options.stage = shaderStage_ == 0 ? asset::ShaderStage::Vertex : asset::ShaderStage::Fragment;
                    options.entryPoint = entryPoint_.data();
                    options.colorSpace = srgb_ ? asset::TextureColorSpace::Srgb : asset::TextureColorSpace::Linear;
                    controller.importFile(entry.path, options);
                }
            }
            auto ext = entry.path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!entry.directory && (ext == ".glb" || ext == ".gltf") && ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(ModelFilePayload, utf8.c_str(), utf8.size() + 1);
                ImGui::TextUnformatted(label.c_str()); ImGui::EndDragDropSource();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
}

} // namespace rubia::editor
