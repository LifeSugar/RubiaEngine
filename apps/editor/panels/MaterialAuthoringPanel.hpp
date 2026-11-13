#pragma once
#include "content/EditorAssetController.hpp"
#include <array>
namespace rubia::editor
{
class MaterialAuthoringPanel final
{
  public:
    void draw(const asset::AssetManager &, EditorAssetController &);

  private:
    asset::AssetDomainId domain_;
    asset::ShaderAssetHandle vertex_, fragment_;
    asset::ShaderProgramAssetHandle program_;
    asset::MaterialTemplateAssetHandle template_;
    std::vector<asset::MaterialTextureSlotMetadata> slots_;
    asset::MaterialAsset::CreateInfo material_;
    std::array<char, 128> name_{'M', 'a', 't', 'e', 'r', 'i', 'a', 'l', '\0'};
};
} // namespace rubia::editor
