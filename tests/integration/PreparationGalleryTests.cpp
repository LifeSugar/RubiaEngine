#include "AppSmokeTests.hpp"
#include "backend/VulkanApplicationBackend.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "EditorLayer.hpp"
#include "gallery/GalleryContent.hpp"
#include "vulkan/VulkanDrawList.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <thread>

namespace rubia::test
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
void saveCapture(const std::filesystem::path& path,
                 const rhi::vulkan::VulkanRenderer::ViewportCapture& capture)
{
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << capture.width << ' ' << capture.height << "\n255\n";
    for (size_t i=0; i<capture.rgba.size(); i+=4)
        out.write(reinterpret_cast<const char*>(capture.rgba.data()+i), 3);
    requireGallery(static_cast<bool>(out), "could not save GPU readback");
}
}

void AppSmokeTests::runPreparationGalleryTest()
{
    using namespace asset;
    using namespace render;
    editor::EditorLayer gui;
    editor::App app;
    editor::App::RunConfig config;
    config.windowWidth = 1600; config.windowHeight = 900;
    config.windowTitle = "Rubia - Preparation Gallery - Material Atelier";
    config.outputMode = render::OutputMode::Editor;
    const auto folder = std::filesystem::path(PROJECT_SOURCE_DIR) / "assets/preparation_gallery";
    const auto artifacts = std::filesystem::current_path() / "gallery-results";
    std::filesystem::create_directories(artifacts);
    try
    {
        app.initWindow(config, false);
        app.initBackend(config);
        app.startEmptyScene(config);
        app.setupCamera();
        app.initImGui(config);
        app.guiRenderBridge.attach(app.backend_->guiHost(), app.renderAssets, app.imguiLayer.rendererBackend());
        awaitGallery([&] {
            app.resourcePreparation().advance(); app.updateContentLoading(); app.window.pollEvents();
            requireGallery(app.contentLoadStatus_.state != editor::ContentLoadState::Failed,
                           app.contentLoadStatus_.message);
            return app.backend_->sceneReady();
        });
        auto& assets = app.assetManager;
        auto& preparation = app.resourcePreparation();
        editor::ApplicationGuiContext context{assets, app.scene, app.guiRenderBridge,
            &app.textureImports, &app.contentLoadStatus_, &preparation, app.demoContent.defaultMaterial};
        gui.attach(context);
        const auto content = editor::loadGalleryContent(context, folder, [&] {
            app.window.pollEvents(); preparation.advance();
        });
        const auto& materials = content.materials;
        const auto& textures = content.textures;
        const auto& roots = content.roots;
        // Admit all roots before advancing: duplicate subscribers must share the same work.
        // Imported meshes also prepare their original glTF materials as dependencies.
        std::vector<ResourcePreparationTicket> tickets;
        size_t started=0, shared=0, cacheHits=0, cancelled=0;
        for(auto root:roots)
        {
            auto a=preparation.prepare(assets,root);
            requireGallery(a.accepted(),a.error);
            requireGallery(a.disposition==ResourcePreparationDisposition::Started,"fresh root was not Started");
            ++started; tickets.push_back(a.ticket);
            auto b=preparation.prepare(assets,root);
            requireGallery(b.accepted() && b.disposition==ResourcePreparationDisposition::Shared,"duplicate did not share preparation");
            ++shared; tickets.push_back(b.ticket);
            auto c=preparation.prepare(assets,root);
            requireGallery(c.accepted(),c.error);
            preparation.cancel(c.ticket);
            requireGallery(preparation.status(c.ticket).state==ResourcePreparationState::Cancelled,"subscriber cancellation failed");
            preparation.release(c.ticket); ++cancelled;
        }
        awaitGallery([&] {
            preparation.advance(); app.window.pollEvents();
            bool ready=true;
            for(auto ticket:tickets) {
                auto status=preparation.status(ticket);
                requireGallery(status.state!=ResourcePreparationState::Failed,status.error);
                ready &= status.state==ResourcePreparationState::Ready;
            }
            return ready;
        });
        for(auto ticket:tickets) preparation.release(ticket);
        for(auto root:roots) {
            const auto result=preparation.prepare(assets,root);
            requireGallery(result.accepted() && result.disposition==ResourcePreparationDisposition::CacheHit,"resident root was not reused");
            preparation.release(result.ticket); ++cacheHits;
        }
        std::vector<MaterialAssetHandle> allMaterials(materials.begin(),materials.end());
        const auto warm=preparation.prewarmPipelines(allMaterials);
        requireGallery(warm.accepted(),warm.error);
        awaitGallery([&] {
            preparation.advance();
            const auto status=preparation.pipelineStatus(warm.ticket);
            requireGallery(status.state!=PipelinePreparationState::Failed,status.error);
            return status.state==PipelinePreparationState::Ready;
        });
        const auto warmCount=preparation.pipelineStatus(warm.ticket).total;
        requireGallery(warmCount==9,"expected 8 specimen PSOs plus 1 environment PSO");
        preparation.releasePipelines(warm.ticket);
        editor::populateGalleryScene(app.scene, content);
        app.camera.setRotation(glm::vec3(-40,0,0));
        app.camera.setPosition(glm::vec3(0,1.9f,0)-app.camera.getForwardVector()*27.0f);
        app.camera.setAspect(16.0f/9.0f);
        vulkanBackend(app).renderer().resizeEditorViewport({1600,900});
        auto frame=app.makeRenderFrame();
        const auto beforeDraw=vulkanBackend(app).renderer().pipelineCacheStatistics();
        auto draw=vulkanBackend(app).renderer().compileDrawList(frame.renderList,app.renderAssets);
        requireGallery(draw.opaque.size()==33 && draw.transparent.size()==8,"expected 33 opaque/clip (including environment) and 8 transparent draws");
        const auto pipelineFor = [&](MaterialAssetHandle material) {
            const auto* gpu=&app.renderAssets.material(material);
            for (const auto& item:draw.opaque) if(item.material==gpu) return item.pipeline.get();
            for (const auto& item:draw.transparent) if(item.material==gpu) return item.pipeline.get();
            throw std::runtime_error("material missing from compiled gallery");
        };
        requireGallery(pipelineFor(materials[5])==pipelineFor(materials[6]), "clip thresholds split the PSO");
        requireGallery(pipelineFor(materials[7])==pipelineFor(materials[8]), "transparent tint/alpha split the PSO");
        requireGallery(pipelineFor(materials[2])!=pipelineFor(materials[3]), "different VS reused the wrong PSO");
        requireGallery(pipelineFor(materials[2])!=pipelineFor(materials[9]), "cull mode reused the wrong PSO");
        std::set<const rhi::PipelineResource*> pipelines;
        for(const auto& d:draw.opaque) pipelines.insert(d.pipeline.get());
        for(const auto& d:draw.transparent) pipelines.insert(d.pipeline.get());
        requireGallery(pipelines.size()==9,"draw compiler failed to share PSOs");
        bool resizeBeforeNextFrame = false;
        const auto renderGalleryFrame = [&](const RenderFrame& renderFrame) {
            for (uint32_t attempt = 0; attempt < 32; ++attempt) {
                app.window.pollEvents();
                if (resizeBeforeNextFrame) {
                    app.reconfigurePresentation(gui);
                    resizeBeforeNextFrame = false;
                }
                const auto slot = vulkanBackend(app).renderer().currentFrameIndex();
                const auto result = app.backend_->render(renderFrame, app.renderAssets);
                resizeBeforeNextFrame = render::needsResize(result);
                if (render::wasSubmitted(result))
                    return slot;
            }
            throw std::runtime_error("Preparation gallery: no frame submitted after swapchain retries");
        };
        uint32_t captureSlot=0;
        for(int i=0;i<4;++i) {
            captureSlot=renderGalleryFrame(frame);
        }
        requireGallery(vulkanBackend(app).renderer().pipelineCacheStatistics().creations==beforeDraw.creations,"draw created a PSO after prewarm");
        const auto capture=vulkanBackend(app).renderer().captureEditorViewport(captureSlot);
        saveCapture(artifacts/"gallery.ppm",capture);
        size_t colored=0;
        for(size_t i=0;i<capture.rgba.size();i+=4) {
            const auto r=capture.rgba[i],g=capture.rgba[i+1],b=capture.rgba[i+2];
            colored += std::max({r,g,b})-std::min({r,g,b})>20;
        }
        requireGallery(colored>10000,"GPU readback is blank or geometry is not visible");
        auto withoutGlass=frame;
        withoutGlass.renderList.transparent.clear();
        withoutGlass.renderList.objectData.clear();
        for(auto& item:withoutGlass.renderList.opaque) {
            withoutGlass.renderList.objectData.push_back(frame.renderList.objectData.at(item.objectIndex));
            item.objectIndex=static_cast<uint32_t>(withoutGlass.renderList.objectData.size()-1);
        }
        const auto noGlassSlot=renderGalleryFrame(withoutGlass);
        const auto backdrop=vulkanBackend(app).renderer().captureEditorViewport(noGlassSlot);
        saveCapture(artifacts/"gallery-without-glass.ppm",backdrop);
        size_t blendedPixels=0;
        for(size_t i=0;i<capture.rgba.size();i+=4) {
            const int delta=std::abs(int(capture.rgba[i])-int(backdrop.rgba[i]))+
                            std::abs(int(capture.rgba[i+1])-int(backdrop.rgba[i+1]))+
                            std::abs(int(capture.rgba[i+2])-int(backdrop.rgba[i+2]));
            blendedPixels += delta>12;
        }
        requireGallery(blendedPixels>3000,"glass layer has no visible effect against the backdrop");
        const auto overviewPosition=app.camera.getPosition();
        const auto overviewRotation=app.camera.getRotation();
        app.camera.setRotation(glm::vec3(-25,0,0));
        app.camera.setPosition(glm::vec3(2.35f,1.45f,3.8f)-app.camera.getForwardVector()*10.5f);
        const auto closeupFrame=app.makeRenderFrame();
        const auto closeupSlot=renderGalleryFrame(closeupFrame);
        saveCapture(artifacts/"gallery-glass-closeup.ppm",vulkanBackend(app).renderer().captureEditorViewport(closeupSlot));
        app.camera.setRotation(overviewRotation); app.camera.setPosition(overviewPosition);
        // Replace one shared texture while frames continue rendering. Every subscriber
        // must switch descriptors without changing material UBOs or pipelines.
        const auto texture=textures.at("checker");
        const auto original=assets.texture(texture);
        std::array<rhi::BindingSetRef,8> oldSets;
        std::array<rhi::BufferRef,8> oldBuffers;
        for(size_t i=2;i<10;++i) {
            oldSets[i-2]=app.renderAssets.material(materials[i]).descriptorSet();
            oldBuffers[i-2]=app.renderAssets.material(materials[i]).parameterBuffer();
        }
        TextureAsset::CreateInfo replacement{original.name(),original.width(),original.height(),
            original.format(),original.colorSpace(),original.sampler(),original.payload(),original.mipLevels()};
        requireGallery(replacement.format==TextureFormat::RGBA8UNorm,"expected decoded RGBA texture");
        for(size_t i=0;i<replacement.payload.size();i+=4)
            for(size_t c=0;c<3;++c) replacement.payload[i+c]=std::byte(255-std::to_integer<unsigned>(replacement.payload[i+c]));
        const auto oldRevision=assets.contentRevision(texture);
        (void)assets.replaceTexture(texture,TextureAsset(std::move(replacement)));
        requireGallery(assets.contentRevision(texture)>oldRevision,"texture revision did not advance");
        const auto replaceAndRender=[&] {
            auto first=preparation.prepare(assets,texture);
            auto duplicate=preparation.prepare(assets,texture);
            requireGallery(first.accepted() && first.disposition==ResourcePreparationDisposition::Started,
                           "new texture revision did not start replacement");
            requireGallery(duplicate.accepted() && duplicate.disposition==ResourcePreparationDisposition::Shared,
                           "new texture revision was not shared");
            awaitGallery([&] {
                app.window.pollEvents(); preparation.advance();
                auto status=preparation.status(first.ticket);
                requireGallery(status.state!=ResourcePreparationState::Failed,status.error);
                captureSlot=renderGalleryFrame(frame);
                return status.state==ResourcePreparationState::Ready;
            });
            requireGallery(preparation.status(duplicate.ticket).state==ResourcePreparationState::Ready,"shared revision did not finish");
            preparation.release(first.ticket); preparation.release(duplicate.ticket);
        };
        replaceAndRender();
        for(size_t i=2;i<10;++i) {
            requireGallery(app.renderAssets.material(materials[i]).descriptorSet()!=oldSets[i-2],"dependent texture descriptor was not replaced");
            requireGallery(app.renderAssets.material(materials[i]).parameterBuffer()==oldBuffers[i-2],"texture replacement reallocated the material UBO");
        }
        const auto updated=vulkanBackend(app).renderer().captureEditorViewport(captureSlot);
        saveCapture(artifacts/"gallery-updated.ppm",updated);
        size_t changedPixels=0;
        for(size_t i=0;i<capture.rgba.size();i+=4)
            changedPixels += std::abs(int(capture.rgba[i])-int(updated.rgba[i]))+
                             std::abs(int(capture.rgba[i+1])-int(updated.rgba[i+1]))+
                             std::abs(int(capture.rgba[i+2])-int(updated.rgba[i+2]))>20;
        requireGallery(changedPixels>10000,"new texture revision was not visible in the rendered image");
        (void)assets.replaceTexture(texture,original);
        replaceAndRender();
        requireGallery(vulkanBackend(app).renderer().pipelineCacheStatistics().creations==beforeDraw.creations,"texture update rebuilt PSOs");
        std::ofstream report(artifacts/"report.txt");
        report << "Visible glass-layer pixels=" << blendedPixels << "\n";
        report << "Live texture revision replacement + restore passed; 8 material descriptors rebound, UBOs and PSOs reused\n"
               << "Updated texture changed pixels=" << changedPixels << '\n';
        report << "40 specimens + architecture / 10 specimen materials + architecture / 4 VS + 6 PS / 7 programs / 9 PSOs\n"
               << "Draws: opaque+clip=" << draw.opaque.size() << " transparent=" << draw.transparent.size()
               << "\nStarted=" << started << " Shared=" << shared << " CacheHit=" << cacheHits
               << " Cancelled subscribers=" << cancelled << "\nPSOs created during draw=0\n"
               << "Material buffers=DeviceLocal via staging\nGPU vertex storage=canonical asset::Vertex\n"
               << "GPU readback colored pixels=" << colored << '\n';
        report.close();
        std::clog << "[Gallery] 41 draws, 10 specimen materials + architecture, 9 warmed PSOs; " << started
                  << " Started / " << shared << " Shared / " << cacheHits << " CacheHit; capture: " << artifacts << '\n';
        draw = {}; // Release retained pipeline owners before the device is destroyed.
        if (app.backend_) app.backend_->waitIdle();
        oldSets = {}; oldBuffers = {};
        gui.detach(); app.cleanup();
    }
    catch (...) {if (app.backend_) app.backend_->waitIdle(); gui.detach(); app.cleanup(); throw;}
}
}
