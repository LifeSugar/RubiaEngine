#include "GalleryContent.hpp"
#include "content/EditorAssetController.hpp"
#include "scene/Scene.hpp"
#include <chrono>
#include <thread>
#include <stdexcept>

namespace rubia::editor
{
namespace
{
void requireGallery(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error("Preparation gallery: " + message);
}
template<class Predicate> void awaitGallery(Predicate done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!done())
    {
        requireGallery(std::chrono::steady_clock::now() < deadline, "preparation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
        constexpr size_t materialCount=11; // Ten specimens plus the architectural background.
        const std::array<const char*,materialCount> names={"00 Porcelain", "01 Vertex pigment", "02 Jade glaze", "03 Cobalt tile",
            "04 Brushed copper", "05 Lattice 0.3", "06 Lattice 0.7", "07 Cyan glass", "08 Rose glass", "09 Two-sided silk", "Atelier architecture"};
}
GalleryContent loadGalleryContent(const ApplicationGuiContext& context,
    const std::filesystem::path& folder, const std::function<void()>& pump)
{
    using namespace asset;
    using namespace render;
    auto& assets = context.assets;
    GalleryContent content;
        editor::EditorAssetController imports;
        editor::EditorSelection selection;
        imports.update(context, selection);
        const auto pumpImport = [&] {
            awaitGallery([&] {
                pump(); imports.update(context, selection);
                return !imports.busy();
            });

        };
        std::map<std::string, ShaderAssetHandle> shaders;
        for (const auto* entry : {"VSBasic", "VSColor", "VSFull", "VSTiled", "PSSolid", "PSColor", "PSTextured", "PSDetail", "PSGlass", "PSStage"})
        {
            editor::EditorAssetController::ImportOptions options;
            options.entryPoint=entry;
            options.stage=entry[0]=='V' ? ShaderStage::Vertex : ShaderStage::Fragment;
            const auto before=assets.shaderHandles().size();
            imports.importFile(folder / "gallery.hlsl", options); pumpImport();
            requireGallery(assets.shaderHandles().size()==before+1, "HLSL import failed: " + std::string(entry) + ": " + imports.message());
            shaders[entry]=assets.shaderHandles().back();
        }
        auto& textures = content.textures;
        for (const auto* name : {"checker", "normal", "orm", "emission"})
        {
            editor::EditorAssetController::ImportOptions options;
            options.colorSpace=(std::string(name)=="normal" || std::string(name)=="orm") ?
                TextureColorSpace::Linear : TextureColorSpace::Srgb;
            imports.importFile(folder / (std::string(name)+".png"), options); pumpImport();
            textures[name]=std::get<TextureAssetHandle>(selection.target());
        }
        const std::array<glm::vec4,materialCount> colors={glm::vec4(.88f,.55f,.25f,1), glm::vec4(1),
            glm::vec4(.17f,.73f,.53f,1), glm::vec4(.18f,.38f,.85f,1), glm::vec4(.95f,.48f,.24f,1),
            glm::vec4(.72f,.58f,.24f,1), glm::vec4(.12f,.64f,.52f,1), glm::vec4(.15f,.74f,.9f,.22f),
            glm::vec4(.92f,.3f,.5f,.28f), glm::vec4(.75f,.28f,.21f,1), glm::vec4(1)};
        auto& materials = content.materials;
        auto& models = content.models;
        std::map<std::pair<std::string,std::string>, MaterialTemplateAssetHandle> templates;
        auto& roots = content.roots;
        for (size_t i=0; i<materialCount; ++i)
        {
            const std::string vs=i==0?"VSBasic":i==1?"VSColor":i==3?"VSTiled":"VSFull";
            const std::string ps=i==0?"PSSolid":i==1?"PSColor":i==4?"PSDetail":(i==7 || i==8)?"PSGlass":i==10?"PSStage":"PSTextured";
            const auto pair=std::make_pair(vs,ps);
            if (!templates.count(pair))
            {
                imports.buildProgram(shaders.at(vs),shaders.at(ps)); pumpImport();
                MaterialTemplateAsset::CreateInfo info;
                info.name=vs+"/"+ps; info.program=imports.draftProgram();
                if (i>=2 && i<10) info.textureSlots.push_back({"color","colorTexture","colorSampler"});
                if (i==4) for (const auto* slot : {"normal", "orm", "emission"})
                    info.textureSlots.push_back({slot,std::string(slot)+"Texture",std::string(slot)+"Sampler"});
                templates[pair]=assets.createMaterialTemplate(std::move(info));
            }
            MaterialAsset::CreateInfo info;
            info.name=names[i]; info.materialTemplate=templates.at(pair);
            info.parameters={{"tint",colors[i]}, {"surface",glm::vec4(
                i==7 || i==8?.12f:i==4?.85f:i==10?.7f:.28f,
                i==4?.85f:i==5?.55f:.08f, .85f, .18f)}};
            if(i>=2 && i<10) info.textures.push_back({"color",textures.at("checker")});
            if(i==4) for(const auto* slot : {"normal","orm","emission"}) info.textures.push_back({slot,textures.at(slot)});
            if(i==5 || i==6) {info.renderState.alphaClipEnabled=true; info.renderState.alphaClipThreshold=i==5?.3f:.7f;}
            if(i==7 || i==8) info.renderState=makeTransparentMaterialState();
            if(i==9) info.renderState.doubleSided=true;
            materials[i]=assets.createMaterial(std::move(info));
            roots.push_back(materials[i]);
            imports.importFile(folder / (i==10?"environment.glb":("group_0"+std::to_string(i)+".glb")), {}); pumpImport();
            models[i]=std::get<ModelAssetHandle>(selection.target());
            for (const auto& node: assets.model(models[i]).nodes())
                for(auto mesh:node.meshes) roots.push_back(mesh);
        }
        imports.stop();
    return content;
}
void populateGalleryScene(scene::Scene& scene, const GalleryContent& content)
{
    for (size_t i = 0; i < materialCount; ++i)
    {
        scene::SceneNode node;
        node.name = names[i];
        node.model = content.models[i];
        node.materialOverride = content.materials[i];
        (void)scene.addNode(std::move(node));
    }
}
}
