#include "vulkan/VulkanScenePreparation.hpp"

#include "asset/MeshAsset.hpp"
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
        if (!request_.materialTemplate || !request_.presentProgram || !request_.maxRenderObjects)
        {
            throw std::invalid_argument(
                "scene requires prepared CPU resource inputs and object capacity");
        }
        const auto domain = request_.materialTemplate.domain;
        if (request_.presentProgram.domain != domain)
        {
            throw std::invalid_argument("scene resources belong to different asset domains");
        }
        if (request_.materialTemplate.data->parameterBlock().descriptor.set != 1)
        {
            throw std::invalid_argument(
                "Vulkan scene materials currently require descriptor set 1");
        }
        for (const auto& mesh : request_.meshes)
        {
            if (!mesh || mesh.domain != domain)
            {
                throw std::invalid_argument("invalid mesh snapshot or asset domain");
            }
            roots_.push_back({mesh, {}, false});
        }
        roots_.push_back({request_.materialTemplate, {}, false});
        roots_.push_back({request_.presentProgram, {}, false});

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
                            if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::MeshAsset>>)
                            {
                                return preparations_.prepareMesh(cache_, source);
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
            const auto materialTemplate =
                cache_.materialTemplate(request_.materialTemplate.version.handle);
            const auto present = cache_.shaderProgram(request_.presentProgram.version.handle);
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
    // Completed GPU objects stay in renderer/cache. CPU scene ownership stays with the caller.
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
