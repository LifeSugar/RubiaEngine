#include "vulkan/VulkanScenePreparation.hpp"

#include "asset/AssetManager.hpp"
#include "vulkan/DefaultPipelineFactory.hpp"
#include "vulkan/RenderAssetCache.hpp"
#include "vulkan/VulkanRenderer.hpp"
#include "vulkan/VulkanResourcePreparation.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace rubia::rhi::vulkan
{
using render::ScenePreparationState;

VulkanScenePreparation::VulkanScenePreparation(VulkanRenderer& renderer, RenderAssetCache& cache,
                                               VulkanResourcePreparation& preparations,
                                               render::SceneResourceRequest request)
    : renderer_(renderer), cache_(cache), preparations_(preparations), request_(std::move(request))
{
}

VulkanScenePreparation::~VulkanScenePreparation()
{
    cancel();
}

void VulkanScenePreparation::begin()
{
    try
    {
        if (!request_.assets || request_.models.empty() || request_.maxRenderObjects == 0)
        {
            throw std::invalid_argument(
                "scene preparation requires CPU assets, models and object capacity");
        }
        const auto& assets = *request_.assets;
        if (!assets.contains(request_.materialTemplate) ||
            !assets.isMaterialTemplateCurrent(request_.materialTemplate) ||
            !assets.contains(request_.presentProgram))
        {
            throw std::invalid_argument(
                "scene preparation references an invalid template or program");
        }
        for (auto model : request_.models)
        {
            for (const auto& node : assets.model(model).nodes())
            {
                for (auto mesh : node.meshes)
                {
                    for (const auto& submesh : assets.mesh(mesh).submeshes())
                    {
                        if (assets.material(submesh.material).materialTemplate() !=
                            request_.materialTemplate)
                        {
                            throw std::invalid_argument("scene preparation requires the requested "
                                                        "shared material template");
                        }
                    }
                }
            }
        }

        if (assets.materialTemplate(request_.materialTemplate).parameterBlock().descriptor.set != 1)
        {
            throw std::invalid_argument(
                "Vulkan scene materials currently require descriptor set 1");
        }
        for (auto model : request_.models)
        {
            roots_.push_back({assets.snapshot(model), {}, false});
        }
        roots_.push_back({assets.snapshot(request_.materialTemplate), {}, false});
        roots_.push_back({assets.snapshot(request_.presentProgram), {}, false});

        // Preconditions on an empty renderer/cache are checked by the public
        // renderer entry point. From here on, all partial resources are ours.
        ownsResources_ = true;
        status_.total = roots_.size();
        status_.state = ScenePreparationState::PreparingResources;
    }
    catch (const std::exception& error)
    {
        fail(error);
    }
}

void VulkanScenePreparation::advance()
{
    try
    {
        if (status_.state == ScenePreparationState::PreparingResources)
        {
            for (auto& root : roots_)
            {
                if (root.ready)
                {
                    continue;
                }
                if (!root.ticket)
                {
                    const auto result = std::visit(
                        [&](const auto& source) -> ResourcePreparationResult
                        {
                            using T = std::decay_t<decltype(source)>;
                            if constexpr (std::is_same_v<T,
                                                         asset::AssetSnapshot<asset::ModelAsset>>)
                            {
                                return preparations_.prepareModel(cache_, source);
                            }
                            else if constexpr (std::is_same_v<T, asset::AssetSnapshot<
                                                                     asset::MaterialTemplateAsset>>)
                            {
                                return preparations_.prepareMaterialTemplate(cache_, source);
                            }
                            else if constexpr (std::is_same_v<T, asset::AssetSnapshot<
                                                                     asset::ShaderProgramAsset>>)
                            {
                                return preparations_.prepareShaderProgram(cache_, source);
                            }
                            else
                            {
                                throw std::logic_error("invalid scene preparation root");
                            }
                        },
                        *root.source);
                    if (result.code == ResourcePreparationCode::QueueFull)
                    {
                        break;
                    }
                    if (!result.accepted())
                    {
                        throw std::runtime_error(result.error);
                    }
                    root.ticket = result.ticket;
                    root.source.reset();
                }
                const auto progress = preparations_.status(root.ticket);
                if (progress.state == ResourcePreparationState::Failed ||
                    progress.state == ResourcePreparationState::Cancelled)
                {
                    throw std::runtime_error("scene resource preparation failed: " +
                                             progress.error);
                }
                if (progress.state == ResourcePreparationState::Ready)
                {
                    preparations_.release(root.ticket);
                    root.ticket = {};
                    root.ready = true;
                    ++status_.completed;
                }
            }
            if (status_.completed == status_.total)
            {
                status_.state = ScenePreparationState::PreparingPipelines;
            }
        }
        else if (status_.state == ScenePreparationState::PreparingPipelines)
        {
            const auto materialTemplate = cache_.materialTemplate(request_.materialTemplate);
            const auto present = cache_.shaderProgram(request_.presentProgram);
            if (!materialTemplate || !present)
            {
                throw std::logic_error("scene pipeline dependencies are missing");
            }
            renderer_.createSceneResources(
                makeDefaultScenePipeline(materialTemplate->program(), materialTemplate->layout()),
                makeDefaultPresentPipeline(present), request_.maxRenderObjects);
            status_.state = ScenePreparationState::Ready;
        }
    }
    catch (const std::exception& error)
    {
        fail(error);
    }
}

void VulkanScenePreparation::activate()
{
    if (status_.state != ScenePreparationState::Ready)
    {
        throw std::logic_error("scene preparation must be ready before activation");
    }
    // Completed GPU objects stay in renderer/cache. The source owner may now
    // move its CPU registries into the live application without backend reads.
    ownsResources_ = false;
    roots_.clear();
    request_ = {};
    status_.state = ScenePreparationState::Activated;
}

void VulkanScenePreparation::discardResources() noexcept
{
    for (auto& root : roots_)
    {
        if (root.ticket)
        {
            preparations_.cancel(root.ticket);
            preparations_.release(root.ticket);
            root.ticket = {};
        }
    }
    roots_.clear();
    if (ownsResources_)
    {
        ownsResources_ = false;
        renderer_.releaseSceneResources();
        // Renderer waits for GPU users. Cancelled in-flight upload leases remain service-owned.
        cache_.reset();
    }
    request_ = {};
}

void VulkanScenePreparation::cancel() noexcept
{
    if (status_.state == ScenePreparationState::Activated ||
        status_.state == ScenePreparationState::Cancelled ||
        status_.state == ScenePreparationState::Failed)
    {
        return;
    }
    discardResources();
    status_.state = ScenePreparationState::Cancelled;
}

void VulkanScenePreparation::fail(const std::exception& error)
{
    status_.error = error.what();
    discardResources();
    status_.state = ScenePreparationState::Failed;
}
} // namespace rubia::rhi::vulkan
