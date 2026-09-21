#include "vulkan/VulkanResourcePreparationBridge.hpp"
#include "vulkan/VulkanRenderer.hpp"
#include <stdexcept>
#include <type_traits>

namespace rubia::rhi::vulkan
{
VulkanResourcePreparationBridge::VulkanResourcePreparationBridge(VulkanRenderer& renderer,
                                                                 RenderAssetCache& cache)
    : renderer_(renderer), cache_(cache)
{
}
VulkanResourcePreparationBridge::~VulkanResourcePreparationBridge()
{
    cancelAll();
}
render::ResourcePreparationResult VulkanResourcePreparationBridge::prepare(
    render::ResourceAssetSnapshot source)
{
    auto result = std::visit(
        [&](auto value)
        {
            using T = decltype(value);
            if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::TextureAsset>>)
            {
                return renderer_.prepareTexture(cache_, std::move(value));
            }
            else if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::MeshAsset>>)
            {
                return renderer_.prepareMesh(cache_, std::move(value));
            }
            else if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::ShaderAsset>>)
            {
                return renderer_.prepareShader(cache_, std::move(value));
            }
            else if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::ShaderProgramAsset>>)
            {
                return renderer_.prepareShaderProgram(cache_, std::move(value));
            }
            else if constexpr (std::is_same_v<T,
                                              asset::AssetSnapshot<asset::MaterialTemplateAsset>>)
            {
                return renderer_.prepareMaterialTemplate(cache_, std::move(value));
            }
            else
            {
                return renderer_.prepareMaterial(cache_, std::move(value));
            }
        },
        std::move(source));
    if (result.accepted())
    {
        try
        {
            tickets_.insert(result.ticket.value);
        }
        catch (...)
        {
            renderer_.cancelResourcePreparation(result.ticket);
            renderer_.releaseResourcePreparation(result.ticket);
            throw;
        }
    }
    return result;
}
void VulkanResourcePreparationBridge::checkTicket(render::ResourcePreparationTicket ticket) const
{
    if (tickets_.find(ticket.value) == tickets_.end())
    {
        throw std::out_of_range("ticket does not belong to this resource preparation session");
    }
}
render::ResourcePreparationStatus VulkanResourcePreparationBridge::status(
    render::ResourcePreparationTicket ticket) const
{
    checkTicket(ticket);
    return renderer_.resourcePreparationStatus(ticket);
}
void VulkanResourcePreparationBridge::cancel(render::ResourcePreparationTicket ticket)
{
    checkTicket(ticket);
    renderer_.cancelResourcePreparation(ticket);
}
void VulkanResourcePreparationBridge::release(render::ResourcePreparationTicket ticket)
{
    checkTicket(ticket);
    renderer_.releaseResourcePreparation(ticket);
    tickets_.erase(ticket.value);
}
void VulkanResourcePreparationBridge::cancelAll() noexcept
{
    for (auto id : tickets_)
    {
        try
        {
            renderer_.cancelResourcePreparation({id});
            renderer_.releaseResourcePreparation({id});
        }
        catch (...)
        {
        }
    }
    tickets_.clear();
}
void VulkanResourcePreparationBridge::advance()
{
    renderer_.advanceResourcePreparation();
}
} // namespace rubia::rhi::vulkan
