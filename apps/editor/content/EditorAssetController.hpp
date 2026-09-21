#pragma once
#include "ApplicationGui.hpp"
#include "EditorSelection.hpp"
#include "asset/AssetManager.hpp"
#include "gltf/GLBTypes.hpp"
#include "scene/Scene.hpp"
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <utility>
#include <variant>

namespace rubia::editor
{
inline constexpr char ModelPayload[] = "RUBIA_MODEL";
inline constexpr char ModelFilePayload[] = "RUBIA_MODEL_FILE";
inline constexpr char MaterialPayload[] = "RUBIA_MATERIAL";
inline constexpr char TexturePayload[] = "RUBIA_TEXTURE";

// Editor-owned CPU import/scene commands. No Vulkan types; all GPU work uses the frontend port.
class EditorAssetController final
{
  public:
    ~EditorAssetController()
    {
        stop();
    }
    struct FileEntry
    {
        std::filesystem::path path;
        bool directory = false;
    };
    struct ImportOptions
    {
        asset::ShaderStage stage = asset::ShaderStage::Vertex;
        std::string entryPoint = "main";
        asset::TextureColorSpace colorSpace = asset::TextureColorSpace::Srgb;
    };
    void browse(std::filesystem::path path);
    void importFile(std::filesystem::path path, ImportOptions options, bool instantiate = false,
                    uint32_t parent = scene::kInvalidSceneNodeIndex);
    void instantiate(asset::ModelAssetHandle model,
                     uint32_t parent = scene::kInvalidSceneNodeIndex);
    void setPosition(uint32_t node, glm::vec3 position);
    void frameScene();
    std::optional<math::Aabb> takeFocus()
    {
        return std::exchange(focus_, {});
    }
    void assignMaterial(uint32_t node, asset::MaterialAssetHandle material);
    void buildProgram(asset::ShaderAssetHandle vertex, asset::ShaderAssetHandle fragment);
    void buildTemplate(asset::MaterialTemplateAsset::CreateInfo info);
    void createMaterial(asset::MaterialAsset::CreateInfo info);
    void update(const ApplicationGuiContext &, EditorSelection &);
    void stop() noexcept;
    [[nodiscard]] const auto &files() const
    {
        return files_;
    }
    [[nodiscard]] const auto &directory() const
    {
        return directory_;
    }
    [[nodiscard]] const auto &message() const
    {
        return message_;
    }
    [[nodiscard]] bool busy() const
    {
        return importFuture_.valid() || !commands_.empty() || !jobs_.empty();
    }
    [[nodiscard]] auto draftProgram() const
    {
        return draftProgram_;
    }
    [[nodiscard]] auto draftTemplate() const
    {
        return draftTemplate_;
    }

  private:
    struct Imported
    {
        std::filesystem::path path;
        ImportOptions options;
        std::string key;
        std::variant<importer::gltf::GLBModel, asset::TextureAsset::CreateInfo,
                     asset::ShaderAsset::CreateInfo>
            data;
        bool instantiate = false;
        uint32_t parent = scene::kInvalidSceneNodeIndex;
    };
    struct Job
    {
        std::vector<render::ResourceAssetHandle> resources;
        std::vector<asset::MaterialAssetHandle> materials;
        std::vector<render::ResourcePreparationTicket> tickets;
        std::size_t admitted = 0;
        render::PipelinePreparationTicket pipeline;
        std::function<void(const ApplicationGuiContext &, EditorSelection &)> publish;
    };
    void enqueueModel(const ApplicationGuiContext &, asset::ModelAssetHandle, uint32_t);
    void publishImport(const ApplicationGuiContext &, Imported, EditorSelection &);
    void release(Job &, bool cancel) noexcept;
    std::deque<std::function<void(const ApplicationGuiContext &, EditorSelection &)>> commands_;
    std::deque<Job> jobs_;
    std::future<Imported> importFuture_;
    std::future<std::pair<std::filesystem::path, std::vector<FileEntry>>> browseFuture_;
    std::filesystem::path browseRequested_;
    std::filesystem::path directory_;
    std::vector<FileEntry> files_;
    std::map<std::string, asset::AnyAssetHandle> imported_;
    asset::AssetDomainId domain_;
    render::ResourcePreparation *preparations_ = nullptr;
    asset::ShaderProgramAssetHandle draftProgram_;
    asset::MaterialTemplateAssetHandle draftTemplate_;
    std::string message_;
    std::optional<math::Aabb> focus_;
};
} // namespace rubia::editor
