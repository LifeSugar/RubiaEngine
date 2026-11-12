#include "pipeline/PipelinePreparation.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace rubia;
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
struct Shader final : rhi::ShaderResource
{
    rhi::DeviceId owner=1; rhi::ShaderCode value{rhi::ShaderStage::Vertex,"main",{0x07230203,1,2,3}};
    rhi::DeviceId deviceId() const noexcept override{return owner;}
    const rhi::ShaderCode& code() const noexcept override{return value;}
};
struct Layout final : rhi::BindingLayoutResource
{
    rhi::BindingLayoutDesc value{{{3,rhi::BindingType::UniformBuffer,1,rhi::shaderStageMask(rhi::ShaderStage::Vertex)}}};
    rhi::DeviceId deviceId() const noexcept override{return 1;}
    const rhi::BindingLayoutDesc& description() const noexcept override{return value;}
};
struct Target final : rhi::PipelineTargetResource
{
    rhi::DeviceId owner=1;
    rhi::AttachmentSignature value{{{rhi::Format::RGBA8UNorm,rhi::Format::Undefined}},rhi::Format::Undefined,1};
    std::vector<uint32_t> key{4,8,15,16,23,42};
    rhi::DeviceId deviceId() const noexcept override{return owner;}
    const rhi::AttachmentSignature& description() const noexcept override{return value;}
    const std::vector<uint32_t>& compatibilityKey() const noexcept override{return key;}
};
struct Pipeline final : rhi::PipelineResource
{
    rhi::GraphicsPipelineDesc value;
    rhi::DeviceId deviceId() const noexcept override{return 1;}
    const rhi::GraphicsPipelineDesc& description() const noexcept override{return value;}
};
struct Device final : rhi::PipelineDevice
{
    mutable std::atomic<size_t> creations{0};
    mutable std::atomic<bool> fail{false};
    rhi::DeviceId deviceId() const noexcept override{return 1;}
    void validateGraphicsPipeline(const rhi::GraphicsPipelineDesc& d) const override
    {
        if(d.shaders.empty()) throw std::invalid_argument("missing shader");
        for(const auto& shader:d.shaders) if(!shader || shader->deviceId()!=deviceId()) throw std::invalid_argument("foreign shader");
        if(d.target && d.target->deviceId()!=deviceId()) throw std::invalid_argument("foreign target");
    }
    rhi::PipelineRef createGraphicsPipeline(const rhi::GraphicsPipelineDesc& d) const override
    {
        validateGraphicsPipeline(d);
        ++creations;
        if(fail.exchange(false)) throw std::runtime_error("injected creation failure");
        auto result=std::make_shared<Pipeline>();result->value=d;return result;
    }
};
rhi::GraphicsPipelineDesc description()
{
    rhi::GraphicsPipelineDesc result;
    result.shaders={std::make_shared<Shader>()};result.bindingLayouts={std::make_shared<Layout>()};
    result.target=std::make_shared<Target>();result.attachments=result.target->description();
    result.specializations={{rhi::ShaderStage::Vertex,17,true}};
    return result;
}
void testKeysAndLifetime()
{
    Device device;render::PipelineLibrary library(device);
    auto base=description();const auto first=library.getOrCreate(base);
    auto equivalent=description();
    equivalent.viewport.viewports[0].width=400;equivalent.viewport.scissors[0].height=700;
    equivalent.multisample.sampleMask={~0u};
    const uint32_t truth=1;std::vector<std::byte> bytes(sizeof(truth));std::memcpy(bytes.data(),&truth,sizeof(truth));
    equivalent.specializations[0].value=bytes;
    require(library.getOrCreate(equivalent)==first,"content-equivalent owners/dynamic values/specialization packing missed cache");
    auto changed=base;
    auto changedTarget=std::make_shared<Target>();changedTarget->key.back()++;
    changed.target=changedTarget;
    require(library.getOrCreate(changed)!=first,"backend pass compatibility content missing from PSO key");
    changed=base;changed.depthStencil.front.reference=9;
    require(render::PipelineKey::from(changed)!=render::PipelineKey::from(base),"static stencil reference missing from key");
    changed=base;changed.colorBlend.attachments[0].writeMask=1;
    require(render::PipelineKey::from(changed)!=render::PipelineKey::from(base),"color mask missing from key");
    changed=base;auto alteredShader=std::make_shared<Shader>();alteredShader->value.spirv.back()++;
    changed.shaders={alteredShader};
    require(library.getOrCreate(changed)!=first,"shader content revision reused old PSO");
    changed=base;auto foreign=std::make_shared<Shader>();foreign->owner=2;changed.shaders={foreign};
    bool rejected=false;try{(void)library.getOrCreate(changed);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"cache hit bypassed device ownership validation");
    auto dynamic=base;dynamic.dynamicStates.push_back(rhi::DynamicState::StencilReference);
    changed=dynamic;changed.depthStencil.front.reference=99;
    require(render::PipelineKey::from(changed)==render::PipelineKey::from(dynamic),"dynamic stencil value entered key");
    std::weak_ptr<const rhi::PipelineResource> held=first;library.clear();
    require(!held.expired(),"cache eviction destroyed retained pipeline");
    bool wrongThread=false;std::thread other([&]{try{library.clear();}catch(const std::logic_error&){wrongThread=true;}});other.join();
    require(wrongThread,"library admitted another publishing thread");
}
void testPreparationFailureAndSharing()
{
    Device device;render::PipelineLibrary library(device);render::PipelinePreparation preparation(device,library,4,1);
    const auto d=description();
    const auto a=preparation.request({d,d});const auto shared=preparation.request({d});
    require(a.accepted() && shared.accepted() && preparation.status(a.ticket).total==1 && device.creations==0,
            "request admission created a pipeline or failed deduplication");
    preparation.cancel(a.ticket);preparation.release(a.ticket);
    const auto finish=[&](render::PipelinePreparationTicket ticket){
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!render::pipelinePreparationFinished(preparation.status(ticket).state)){
            preparation.advance();require(std::chrono::steady_clock::now()<deadline,"pipeline worker timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    finish(shared.ticket);
    require(preparation.status(shared.ticket).state==render::PipelinePreparationState::Ready && library.statistics().creations==1,
            "one cancelled subscriber cancelled shared work");
    preparation.release(shared.ticket);
    auto failed=d;failed.rasterization.cullMode=rhi::CullMode::Front;
    device.fail=true;auto ticket=preparation.request({failed});finish(ticket.ticket);
    require(preparation.status(ticket.ticket).state==render::PipelinePreparationState::Failed && !library.find(failed),
            "failed worker result entered pipeline library");
    preparation.release(ticket.ticket);ticket=preparation.request({failed});finish(ticket.ticket);
    require(preparation.status(ticket.ticket).state==render::PipelinePreparationState::Ready,"retry after creation failure failed");
    preparation.release(ticket.ticket);
}
}
int main()
{
    try {testKeysAndLifetime();testPreparationFailureAndSharing();std::cout<<"Pipeline library tests passed (keys, compatibility, ownership, worker failure/retry).\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
