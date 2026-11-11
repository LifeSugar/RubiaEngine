#include "resources/ResourcePreparationSession.hpp"
#include <stdexcept>
#include <type_traits>

namespace rubia::render
{
ResourcePreparationSession::ResourcePreparationSession(ResourcePreparationHost &renderer,
                                                       RenderAssetStore &cache)
    : renderer_(renderer), cache_(cache)
{
}
ResourcePreparationSession::~ResourcePreparationSession() { cancelAll(); }
render::ResourcePreparationResult
ResourcePreparationSession::prepare(render::ResourceAssetSnapshot source)
{
    if (!renderer_.canPrepareResources())
    {
        return {ResourcePreparationCode::InvalidRequest,
                {},
                "resource preparation requires presentation and no scene preparation"};
    }
    auto &service = renderer_.resourcePreparationService();
    auto result = std::visit(
        [&](auto value)
        {
            using T = decltype(value);
            if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::TextureAsset>>)
            {
                return service.prepareTexture(cache_, std::move(value));
            }
            else if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::MeshAsset>>)
            {
                return service.prepareMesh(cache_, std::move(value));
            }
            else if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::ShaderAsset>>)
            {
                return service.prepareShader(cache_, std::move(value));
            }
            else if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::ShaderProgramAsset>>)
            {
                return service.prepareShaderProgram(cache_, std::move(value));
            }
            else if constexpr (std::is_same_v<T,
                                              asset::AssetSnapshot<asset::MaterialTemplateAsset>>)
            {
                return service.prepareMaterialTemplate(cache_, std::move(value));
            }
            else
            {
                return service.prepareMaterial(cache_, std::move(value));
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
            renderer_.resourcePreparationService().cancel(result.ticket);
            renderer_.resourcePreparationService().release(result.ticket);
            throw;
        }
    }
    return result;
}
void ResourcePreparationSession::setPublicationTransaction(
    render::ResourcePreparationTicket ticket,
    std::shared_ptr<render::ResourcePublicationTransaction> publication)
{
    checkTicket(ticket);
    renderer_.resourcePreparationService().setPublicationTransaction(ticket,
                                                                     std::move(publication));
}
void ResourcePreparationSession::checkTicket(render::ResourcePreparationTicket ticket) const
{
    if (tickets_.find(ticket.value) == tickets_.end())
    {
        throw std::out_of_range("ticket does not belong to this resource preparation session");
    }
}
render::ResourcePreparationStatus
ResourcePreparationSession::status(render::ResourcePreparationTicket ticket) const
{
    checkTicket(ticket);
    return renderer_.resourcePreparationService().status(ticket);
}
void ResourcePreparationSession::cancel(render::ResourcePreparationTicket ticket)
{
    checkTicket(ticket);
    renderer_.resourcePreparationService().cancel(ticket);
}
void ResourcePreparationSession::release(render::ResourcePreparationTicket ticket)
{
    checkTicket(ticket);
    renderer_.resourcePreparationService().release(ticket);
    tickets_.erase(ticket.value);
}
render::PipelinePreparationResult ResourcePreparationSession::prewarmPipelines(
    const std::vector<asset::MaterialAssetHandle> &materials)
{
    auto result = renderer_.prewarmScenePipelines(cache_, materials);
    if (result.accepted())
    {
        try
        {
            pipelineTickets_.insert(result.ticket.value);
        }
        catch (...)
        {
            renderer_.cancelPipelinePreparation(result.ticket);
            renderer_.releasePipelinePreparation(result.ticket);
            throw;
        }
    }
    return result;
}
void ResourcePreparationSession::checkPipelineTicket(render::PipelinePreparationTicket ticket) const
{
    if (!pipelineTickets_.count(ticket.value))
        throw std::out_of_range("pipeline ticket does not belong to this session");
}
render::PipelinePreparationStatus
ResourcePreparationSession::pipelineStatus(render::PipelinePreparationTicket ticket) const
{
    checkPipelineTicket(ticket);
    return renderer_.pipelinePreparationStatus(ticket);
}
void ResourcePreparationSession::cancelPipelines(render::PipelinePreparationTicket ticket)
{
    checkPipelineTicket(ticket);
    renderer_.cancelPipelinePreparation(ticket);
}
void ResourcePreparationSession::releasePipelines(render::PipelinePreparationTicket ticket)
{
    checkPipelineTicket(ticket);
    renderer_.releasePipelinePreparation(ticket);
    pipelineTickets_.erase(ticket.value);
}
void ResourcePreparationSession::cancelAll() noexcept
{
    for (auto id : tickets_)
    {
        try
        {
            renderer_.resourcePreparationService().cancel({id});
            renderer_.resourcePreparationService().release({id});
        }
        catch (...)
        {
        }
    }
    tickets_.clear();
    for (auto id : pipelineTickets_)
    {
        try
        {
            renderer_.cancelPipelinePreparation({id});
            renderer_.releasePipelinePreparation({id});
        }
        catch (...)
        {
        }
    }
    pipelineTickets_.clear();
}
void ResourcePreparationSession::advance() { renderer_.advanceResourcePreparation(); }
} // namespace rubia::render
