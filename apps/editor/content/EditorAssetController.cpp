#include "content/EditorAssetController.hpp"
#include "gltf/GLBLoader.hpp"
#include "gltf/GLBModelImporter.hpp"
#include "render/SceneRenderExtractor.hpp"
#include "render/SceneResourcePreparation.hpp"
#include "shader/HlslShaderCompiler.hpp"
#include "shader/SpirvShaderImporter.hpp"
#include "texture/KtxTextureImporter.hpp"
#include "texture/StbImageDecoder.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace rubia::editor
{
namespace
{
std::string extension(const std::filesystem::path &path)
{
    auto value = path.extension().string();
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
bool supported(const std::filesystem::path &path)
{
    const auto ext = extension(path);
    return ext == ".glb" || ext == ".gltf" || ext == ".hlsl" || ext == ".spv" || ext == ".png" ||
           ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".ktx2";
}
std::string importKey(const std::filesystem::path &path,
                      const EditorAssetController::ImportOptions &options)
{
    auto key = path.generic_u8string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    const auto ext = extension(path);
    if (ext == ".spv" || ext == ".hlsl")
        key += "|" + std::to_string(static_cast<int>(options.stage)) + "|" + options.entryPoint;
    else if (ext != ".glb" && ext != ".gltf")
        key += "|" + std::to_string(static_cast<int>(options.colorSpace));
    return key + "|" +
           std::to_string(std::filesystem::last_write_time(path).time_since_epoch().count()) + "|" +
           std::to_string(std::filesystem::file_size(path));
}
} // namespace

void EditorAssetController::browse(std::filesystem::path path)
{
    browseRequested_ = std::move(path);
}

void EditorAssetController::importFile(std::filesystem::path path, ImportOptions options,
                                       bool addToScene, uint32_t parent)
{
    commands_.push_back([this, path = std::move(path), options, addToScene,
                         parent](const auto &context, auto &selection) {
        const auto absolute = std::filesystem::canonical(path);
        const auto key = importKey(absolute, options);
        if (const auto found = imported_.find(key);
            found != imported_.end() && extension(absolute) != ".hlsl")
        {
            if (auto model = std::get_if<asset::ModelAssetHandle>(&found->second))
            {
                if (addToScene)
                    enqueueModel(context, *model, parent);
                else
                    selection.select(*model);
            }
            else if (auto texture = std::get_if<asset::TextureAssetHandle>(&found->second))
                selection.select(*texture);
            message_ = "Reused imported asset: " + absolute.filename().u8string();
            return;
        }
        message_ = "Importing " + absolute.filename().u8string();
        importFuture_ =
            std::async(std::launch::async, [absolute, options, addToScene, parent, key] {
                Imported result;
                result.path = absolute;
                result.options = options;
                result.key = key;
                result.instantiate = addToScene;
                result.parent = parent;
                const auto ext = extension(absolute);
                if (ext == ".glb" || ext == ".gltf")
                {
                    importer::gltf::GLBLoader loader;
                    auto model = loader.load(absolute.u8string());
                    if (!model)
                        throw std::runtime_error(loader.getLastError());
                    importer::texture::StbImageDecoder decoder;
                    for (auto &texture : model->textures)
                    {
                        if (texture.storage == importer::gltf::GLBTextureStorage::Rgba8Payload)
                            continue;
                        auto decoded =
                            texture.storage == importer::gltf::GLBTextureStorage::EncodedBytes
                                ? decoder.decodeMemory(texture.data, texture.name)
                                : decoder.decodeFile(absolute.parent_path() /
                                                         std::filesystem::u8path(texture.uri),
                                                     texture.name);
                        texture.width = decoded.width;
                        texture.height = decoded.height;
                        texture.data.resize(decoded.payload.size());
                        std::memcpy(texture.data.data(), decoded.payload.data(),
                                    decoded.payload.size());
                        texture.storage = importer::gltf::GLBTextureStorage::Rgba8Payload;
                    }
                    result.data = std::move(*model);
                }
                else if (ext == ".hlsl")
                    result.data = importer::shader::HlslShaderCompiler{}.compile(
                        absolute, options.stage, options.entryPoint);
                else if (ext == ".spv")
                {
                    asset::AssetManager temporary;
                    importer::shader::SpirvShaderImporter::CreateInfo info;
                    info.assets = &temporary;
                    info.path = absolute;
                    info.name = absolute.filename().u8string();
                    info.stage = options.stage;
                    info.entryPoint = options.entryPoint;
                    const auto handle = importer::shader::SpirvShaderImporter{}.import(info);
                    const auto &shader = temporary.shader(handle);
                    result.data = asset::ShaderAsset::CreateInfo{
                        absolute.filename().u8string(), options.stage, options.entryPoint,
                        shader.spirv(), shader.interface()};
                }
                else
                {
                    if (!supported(absolute))
                        throw std::invalid_argument("Unsupported asset file type");
                    auto texture =
                        ext == ".ktx2"
                            ? importer::texture::KtxTextureImporter{}.importFile(absolute, {})
                            : importer::texture::StbImageDecoder{}.decodeFile(absolute);
                    if (ext != ".ktx2")
                        texture.colorSpace = options.colorSpace;
                    result.data = std::move(texture);
                }
                return result;
            });
    });
}

void EditorAssetController::publishImport(const ApplicationGuiContext &context, Imported value,
                                          EditorSelection &selection)
{
    if (importKey(value.path, value.options) != value.key)
        throw std::runtime_error("Source file changed during import; import it again");
    auto &assets = context.assets;
    asset::AnyAssetHandle imported;
    if (auto source = std::get_if<importer::gltf::GLBModel>(&value.data))
    {
        if (source->meshes.empty())
            throw std::runtime_error("Model contains no meshes");
        importer::gltf::GLBModelImporter::CreateInfo info;
        info.assets = &assets;
        info.baseDirectory = value.path.parent_path();
        info.fallbackMaterial = context.defaultModelMaterial;
        if (!assets.contains(info.fallbackMaterial))
            throw std::runtime_error("Default model material is not ready");
        const auto &material = assets.material(info.fallbackMaterial);
        info.materialMapping = {material.materialTemplate(),
                                "baseColorFactor",
                                "metallicFactor",
                                "roughnessFactor",
                                "emissiveFactor",
                                "baseColorTexture",
                                "metallicRoughnessTexture",
                                "normalTexture",
                                "occlusionTexture",
                                "emissiveTexture"};
        const auto textureFor = [&](const char *name) {
            for (const auto &slot :
                 assets.materialTemplate(material.materialTemplate()).textureSlots())
                if (slot.name == name)
                    return material.textures().at(slot.slot);
            throw std::runtime_error(std::string("Default model material is missing slot: ") +
                                     name);
        };
        info.defaultTexture = textureFor("baseColorTexture");
        info.defaultDataTexture = textureFor("metallicRoughnessTexture");
        info.defaultNormalTexture = textureFor("normalTexture");
        const auto result = importer::gltf::GLBModelImporter{}.import(*source, info);
        imported = result.model;
        if (value.instantiate)
            enqueueModel(context, result.model, value.parent);
        else
            selection.select(result.model);
    }
    else if (auto texture = std::get_if<asset::TextureAsset::CreateInfo>(&value.data))
    {
        const auto handle = assets.createTexture(std::move(*texture));
        imported = handle;
        Job job;
        job.resources.push_back(handle);
        job.publish = [handle](const auto &, auto &target) { target.select(handle); };
        jobs_.push_back(std::move(job));
    }
    else
    {
        const auto handle =
            assets.createShader(std::move(std::get<asset::ShaderAsset::CreateInfo>(value.data)));
        imported = handle;
    }
    imported_[value.key] = imported;
    message_ = "Imported " + value.path.filename().u8string();
}

void EditorAssetController::enqueueModel(const ApplicationGuiContext &context,
                                         asset::ModelAssetHandle model, uint32_t parent)
{
    Job job;
    for (auto mesh : render::collectModelMeshes(context.assets, {model}))
    {
        job.resources.push_back(mesh);
        for (const auto &submesh : context.assets.mesh(mesh).submeshes())
            if (std::find(job.materials.begin(), job.materials.end(), submesh.material) ==
                job.materials.end())
                job.materials.push_back(submesh.material);
    }
    if (job.resources.empty())
        throw std::runtime_error("Model has no renderable meshes");
    job.publish = [this, model, parent](const auto &ctx, auto &selection) {
        // Match the current scene's default frame-object budget. Count instances, not unique
        // meshes.
        scene::Scene candidate(
            {"candidate", {{"candidate", glm::mat4(1), scene::kInvalidSceneNodeIndex, model}}});
        const auto count = render::SceneRenderExtractor{}.extract(ctx.scene, ctx.assets).size() +
                           render::SceneRenderExtractor{}.extract(candidate, ctx.assets).size();
        if (count > 1024)
            throw std::runtime_error("Scene exceeds the current 1024 draw-object capacity");
        scene::SceneNode node;
        node.name = ctx.assets.model(model).name();
        node.model = model;
        node.parent = parent;
        selection.select(SceneNodeTarget{ctx.scene.addNode(std::move(node))});
        frameScene();
    };
    jobs_.push_back(std::move(job));
    message_ = "Preparing model resources and pipelines...";
}
void EditorAssetController::instantiate(asset::ModelAssetHandle model, uint32_t parent)
{
    commands_.push_back([this, model, parent](const auto &context, auto &) {
        enqueueModel(context, model, parent);
    });
}
void EditorAssetController::setPosition(uint32_t node, glm::vec3 position)
{
    commands_.push_back([node, position](const auto &context, auto &) {
        auto transform = context.scene.nodes().at(node).localTransform;
        transform[3] = glm::vec4(position, 1);
        context.scene.setLocalTransform(node, transform);
    });
}
void EditorAssetController::frameScene()
{
    commands_.push_back([this](const auto &context, auto &) {
        const auto candidates =
            render::SceneRenderExtractor{}.extract(context.scene, context.assets);
        if (candidates.empty())
            return;
        auto bounds = candidates.front().worldBounds;
        for (const auto &candidate : candidates)
        {
            bounds.minimum = glm::min(bounds.minimum, candidate.worldBounds.minimum);
            bounds.maximum = glm::max(bounds.maximum, candidate.worldBounds.maximum);
        }
        focus_ = bounds;
    });
}
void EditorAssetController::assignMaterial(uint32_t node, asset::MaterialAssetHandle material)
{
    commands_.push_back([this, node, material](const auto &context, auto &) {
        if (node >= context.scene.nodes().size())
            throw std::out_of_range("Scene instance no longer exists");
        if (!material)
        {
            context.scene.setMaterialOverride(node, {});
            return;
        }
        Job job;
        job.resources.push_back(material);
        job.materials.push_back(material);
        job.publish = [node, material](const auto &ctx, auto &) {
            ctx.scene.setMaterialOverride(node, material);
        };
        jobs_.push_back(std::move(job));
    });
}
void EditorAssetController::buildProgram(asset::ShaderAssetHandle vertex,
                                         asset::ShaderAssetHandle fragment)
{
    commands_.push_back([this, vertex, fragment](const auto &context, auto &) {
        draftProgram_ = context.assets.createShaderProgram({"Editor Program", {vertex, fragment}});
        draftTemplate_ = {};
        message_ = "Program reflected. Pair each texture with its sampler.";
    });
}
void EditorAssetController::buildTemplate(asset::MaterialTemplateAsset::CreateInfo info)
{
    commands_.push_back([this, info = std::move(info)](const auto &context, auto &) {
        draftTemplate_ = context.assets.createMaterialTemplate(info);
        message_ = "Material template ready. Set parameters and textures.";
    });
}
void EditorAssetController::createMaterial(asset::MaterialAsset::CreateInfo info)
{
    commands_.push_back([this, info = std::move(info)](const auto &context, auto &) {
        auto handle = context.assets.createMaterial(info);
        Job job;
        job.resources.push_back(handle);
        job.materials.push_back(handle);
        job.publish = [handle](const auto &, auto &selection) { selection.select(handle); };
        jobs_.push_back(std::move(job));
    });
}
void EditorAssetController::release(Job &job, bool cancel) noexcept
{
    if (!preparations_)
        return;
    for (auto ticket : job.tickets)
    {
        try
        {
            if (cancel)
                preparations_->cancel(ticket);
            preparations_->release(ticket);
        }
        catch (...)
        {
        }
    }
    job.tickets.clear();
    if (job.pipeline)
    {
        try
        {
            if (cancel)
                preparations_->cancelPipelines(job.pipeline);
            preparations_->releasePipelines(job.pipeline);
        }
        catch (...)
        {
        }
        job.pipeline = {};
    }
}
void EditorAssetController::stop() noexcept
{
    for (auto &job : jobs_)
        release(job, true);
    jobs_.clear();
    commands_.clear();
    focus_.reset();
    if (importFuture_.valid())
    {
        try
        {
            importFuture_.get();
        }
        catch (...)
        {
        }
    }
    if (browseFuture_.valid())
    {
        try
        {
            browseFuture_.get();
        }
        catch (...)
        {
        }
    }
    preparations_ = nullptr;
}
void EditorAssetController::update(const ApplicationGuiContext &context, EditorSelection &selection)
{
    using namespace std::chrono_literals;
    // A registry replacement invalidates every queued handle and worker result.
    if (domain_ != context.assets.domain())
    {
        stop();
        imported_.clear();
        draftProgram_ = {};
        draftTemplate_ = {};
        domain_ = context.assets.domain();
    }
    preparations_ = context.preparations;
    try
    {
        if (browseFuture_.valid() && browseFuture_.wait_for(0ms) == std::future_status::ready)
        {
            auto result = browseFuture_.get();
            directory_ = std::move(result.first);
            files_ = std::move(result.second);
        }
        if (!browseRequested_.empty() && !browseFuture_.valid())
        {
            auto path = std::exchange(browseRequested_, {});
            browseFuture_ = std::async(std::launch::async, [path] {
                const auto absolute = std::filesystem::canonical(path);
                std::vector<FileEntry> entries;
                for (const auto &entry : std::filesystem::directory_iterator(absolute))
                {
                    if (entry.is_directory() ||
                        (entry.is_regular_file() && supported(entry.path())))
                        entries.push_back({entry.path(), entry.is_directory()});
                }
                std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
                    return a.directory != b.directory ? a.directory
                                                      : a.path.filename() < b.path.filename();
                });
                return std::make_pair(absolute, std::move(entries));
            });
        }
        if (!context.preparations || context.assets.materialHandles().empty())
            return;
        if (importFuture_.valid())
        {
            if (importFuture_.wait_for(0ms) == std::future_status::ready)
                publishImport(context, importFuture_.get(), selection);
        }
        else if (!commands_.empty())
        {
            auto command = std::move(commands_.front());
            commands_.pop_front();
            command(context, selection);
        }
    }
    catch (const std::exception &error)
    {
        message_ = error.what();
        std::cerr << "[Asset Import] " << message_ << '\n';
    }
    if (!preparations_)
        return;
    // Bound main-thread admission; preparation workers own native creation.
    if (jobs_.empty())
        return;
    auto &job = jobs_.front();
    try
    {
        // Reserve before admission so an allocation failure cannot lose a ticket.
        job.tickets.reserve(job.resources.size());
        unsigned budget = 4;
        while (job.admitted < job.resources.size() && budget--)
        {
            auto result = preparations_->prepare(context.assets, job.resources[job.admitted]);
            if (result.code == render::ResourcePreparationCode::QueueFull)
                break;
            if (!result.accepted())
                throw std::runtime_error(result.error);
            job.tickets.push_back(result.ticket);
            ++job.admitted;
        }
        bool ready = job.admitted == job.resources.size();
        for (auto ticket : job.tickets)
        {
            const auto state = preparations_->status(ticket);
            if (state.state == render::ResourcePreparationState::Failed ||
                state.state == render::ResourcePreparationState::Cancelled)
                throw std::runtime_error(state.error.empty() ? "Resource preparation cancelled"
                                                             : state.error);
            ready &= state.state == render::ResourcePreparationState::Ready;
        }
        if (!ready)
            return;
        if (!job.materials.empty())
        {
            if (!job.pipeline)
            {
                auto result = preparations_->prewarmPipelines(job.materials);
                if (result.code == render::ResourcePreparationCode::QueueFull)
                    return;
                if (!result.accepted())
                    throw std::runtime_error(result.error);
                job.pipeline = result.ticket;
            }
            auto state = preparations_->pipelineStatus(job.pipeline);
            if (!render::pipelinePreparationFinished(state.state))
                return;
            if (state.state != render::PipelinePreparationState::Ready)
                throw std::runtime_error(state.error.empty() ? "Pipeline preparation cancelled"
                                                             : state.error);
        }
        if (job.publish)
            job.publish(context, selection);
        release(job, false);
        jobs_.pop_front();
        message_ = "Ready";
    }
    catch (const std::exception &error)
    {
        message_ = error.what();
        std::cerr << "[Asset Preparation] " << message_ << '\n';
        release(job, true);
        jobs_.pop_front();
    }
}
} // namespace rubia::editor
