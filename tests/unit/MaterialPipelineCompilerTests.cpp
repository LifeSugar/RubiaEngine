#include "pipeline/MaterialPipelineCompiler.hpp"

#include "asset/MeshAsset.hpp"
#include "render/RenderData.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
using namespace rubia;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template <class Action> void rejects(Action action, const char* reason)
{
    try
    {
        action();
    }
    catch (const std::invalid_argument& error)
    {
        require(std::string(error.what()).find(reason) != std::string::npos,
                "pipeline policy rejected a fixture for the wrong reason");
        return;
    }
    throw std::runtime_error(std::string("invalid scene ABI accepted: ") + reason);
}

struct Fixture
{
    asset::ShaderProgramInterface program;
    asset::ShaderInterface fragment;
    std::vector<rhi::BindingLayoutDesc> layouts;
    uint32_t materialGroup = 1;
    uint32_t colorCount = 1;
    bool provideFragment = true;

    Fixture()
    {
        const auto vertex = asset::shaderStageMask(asset::ShaderStage::Vertex);
        const auto pixel = asset::shaderStageMask(asset::ShaderStage::Fragment);
        program.bindings = {
            {0, 0, asset::ShaderResourceType::UniformBuffer, 1, vertex, {}, {}},
            {1, 7, asset::ShaderResourceType::UniformBuffer, 1, pixel, {}, {}},
            {1, 9, asset::ShaderResourceType::SampledImage, 2, pixel, {}, {}}};
        program.vertexInputs = {{"uv", 3, asset::ShaderValueType::Float2},
                                {"position", 0, asset::ShaderValueType::Float3}};
        program.fragmentOutputs = {{"color", 0, asset::ShaderValueType::Float4}};
        program.pushConstants = {{0, 12, vertex | pixel}};
        fragment.stage = asset::ShaderStage::Fragment;
        fragment.specializationConstants = {{render::AlphaClipSpecializationId,
                                             asset::ShaderValueType::Bool}};
        fragment.pushConstants = {{"Draw", 12, asset::ShaderStage::Fragment, 0,
                                   {{"threshold", asset::ShaderValueType::Float, 8, 4, 1}}}};
        layouts = {
            {{{0, rhi::BindingType::UniformBuffer, 1,
               rhi::shaderStageMask(rhi::ShaderStage::Vertex)}}},
            {{{7, rhi::BindingType::UniformBuffer, 1,
               rhi::shaderStageMask(rhi::ShaderStage::Fragment)},
              {9, rhi::BindingType::SampledTexture, 2,
               rhi::shaderStageMask(rhi::ShaderStage::Fragment)}}}};
    }

    render::PipelinePolicyDesc compile(const asset::MaterialRenderState& state = {}) const
    {
        const render::MaterialPipelineInput input{
            program, provideFragment ? &fragment : nullptr, materialGroup, layouts, colorCount};
        return render::MaterialPipelineCompiler{}.compile(
            input, render::makePipelineVariantKey({0, 1}, state));
    }
};

void testMaterialPolicy()
{
    Fixture fixture;
    const auto opaque = fixture.compile();
    require(opaque.depthTestEnabled == true && opaque.depthWriteEnabled == true &&
                opaque.depthCompare == rhi::CompareOp::LessEqual &&
                opaque.cullMode == rhi::CullMode::Back && !opaque.colorBlend->enabled,
            "opaque material did not retain its depth-writing policy");

    auto state = asset::makeTransparentMaterialState();
    state.doubleSided = true;
    const auto transparent = fixture.compile(state);
    require(transparent.depthTestEnabled == true && transparent.depthWriteEnabled == false &&
                transparent.cullMode == rhi::CullMode::None && transparent.colorBlend->enabled,
            "transparent double-sided material did not select its pipeline policy");
    require(transparent.colorBlend->sourceColor == rhi::BlendFactor::SourceAlpha &&
                transparent.colorBlend->destinationColor == rhi::BlendFactor::OneMinusSourceAlpha &&
                transparent.colorBlend->sourceAlpha == rhi::BlendFactor::One &&
                transparent.colorBlend->destinationAlpha == rhi::BlendFactor::OneMinusSourceAlpha,
            "transparent policy does not implement straight-alpha compositing");

    state.depth.testEnabled = false;
    state.depth.compare = asset::DepthCompare::Greater;
    require(fixture.compile(state).depthCompare == rhi::CompareOp::Always,
            "ignored depth comparison was not canonicalized by the material key");
}

void testCanonicalVertexAbi()
{
    const auto policy = Fixture{}.compile();
    require(policy.vertexLayout.attributes.size() == 2 && policy.vertexLayout.bindings.size() == 1,
            "unused canonical vertex attributes were not removed");
    asset::Vertex vertices[2];
    vertices[1].position = {2.0f, 3.0f, 5.0f};
    vertices[1].texCoord = {0.25f, 0.75f};
    const auto* bytes = reinterpret_cast<const unsigned char*>(vertices);
    for (const auto& attribute : policy.vertexLayout.attributes)
    {
        const auto* data = bytes + policy.vertexLayout.bindings.front().stride + attribute.offset;
        if (attribute.location == 0)
        {
            float values[3];
            std::memcpy(values, data, sizeof(values));
            require(attribute.format == rhi::VertexFormat::Float3 && values[0] == 2.0f &&
                        values[1] == 3.0f && values[2] == 5.0f,
                    "position layout does not address canonical mesh bytes");
        }
        else if (attribute.location == 3)
        {
            float values[2];
            std::memcpy(values, data, sizeof(values));
            require(attribute.format == rhi::VertexFormat::Float2 && values[0] == 0.25f &&
                        values[1] == 0.75f,
                    "UV layout does not address canonical mesh bytes");
        }
        else
            throw std::runtime_error("unexpected unused vertex attribute");
    }
    require(policy.constantRanges.size() == 1 && policy.constantRanges[0].offset == 0 &&
                policy.constantRanges[0].size == sizeof(render::DrawPushConstants),
            "draw constants do not cover the renderer draw ABI");
}

void testAlphaClipContract()
{
    Fixture fixture;
    auto state = asset::makeOpaqueMaterialState();
    const auto disabled = fixture.compile(state);
    state.alphaClipEnabled = true;
    state.alphaClipThreshold = 0.2f;
    const auto clipped = fixture.compile(state);
    require(disabled.specializations.size() == 1 &&
                !std::get<bool>(disabled.specializations[0].value) &&
                clipped.specializations.size() == 1 &&
                clipped.specializations[0].stage == rhi::ShaderStage::Fragment &&
                clipped.specializations[0].id == render::AlphaClipSpecializationId &&
                std::get<bool>(clipped.specializations[0].value),
            "AlphaClip is not represented as a typed fragment specialization");
    state.alphaClipThreshold = 0.8f;
    const auto thresholdChanged = fixture.compile(state);
    require(thresholdChanged.specializations[0].value == clipped.specializations[0].value,
            "threshold data leaked into the pipeline specialization");

    fixture.fragment.specializationConstants.clear();
    require(fixture.compile().specializations.empty(),
            "shader without AlphaClip support acquired a specialization");
    rejects([&] { (void)fixture.compile(state); }, "does not support");
    fixture = {};
    fixture.provideFragment = false;
    rejects([&] { (void)fixture.compile(state); }, "does not support");
    fixture = {};
    fixture.fragment.specializationConstants[0].type = asset::ShaderValueType::UInt;
    rejects([&] { (void)fixture.compile(state); }, "must be bool");
    fixture = {};
    fixture.fragment.pushConstants[0].members[0].offset = 4;
    rejects([&] { (void)fixture.compile(state); }, "threshold");
}

void testInvalidAbi()
{
    Fixture fixture;
    fixture.materialGroup = 2;
    rejects([&] { (void)fixture.compile(); }, "group 1");
    fixture = {};
    fixture.layouts.pop_back();
    rejects([&] { (void)fixture.compile(); }, "binding layouts");
    fixture = {};
    fixture.program.bindings.back().set = 2;
    rejects([&] { (void)fixture.compile(); }, "outside frame/material");
    fixture = {};
    fixture.layouts[1].bindings.back().count = 1;
    rejects([&] { (void)fixture.compile(); }, "incompatible");
    fixture = {};
    fixture.layouts[1].bindings.front().type = rhi::BindingType::StorageBuffer;
    rejects([&] { (void)fixture.compile(); }, "incompatible");
    fixture = {};
    fixture.layouts[1].bindings.front().stages = rhi::shaderStageMask(rhi::ShaderStage::Vertex);
    rejects([&] { (void)fixture.compile(); }, "incompatible");
    fixture = {};
    fixture.program.pushConstants.front().offset = UINT32_MAX;
    rejects([&] { (void)fixture.compile(); }, "DrawPushConstants");
    fixture = {};
    fixture.program.pushConstants.front().size = 16;
    rejects([&] { (void)fixture.compile(); }, "DrawPushConstants");
    fixture = {};
    fixture.program.vertexInputs.back().type = asset::ShaderValueType::Float4;
    rejects([&] { (void)fixture.compile(); }, "asset::Vertex");
    fixture = {};
    fixture.program.vertexInputs.back().location = 7;
    rejects([&] { (void)fixture.compile(); }, "asset::Vertex");
    fixture = {};
    fixture.program.fragmentOutputs.front().location = 1;
    rejects([&] { (void)fixture.compile(); }, "color attachments");
    fixture = {};
    fixture.colorCount = 0;
    fixture.program.fragmentOutputs.clear();
    (void)fixture.compile(); // Depth-only passes remain valid without color outputs.
}

void testDefaultsAndUnsupportedFeatures()
{
    const auto scene = render::defaultScenePipelinePolicy();
    require(!scene.cullMode && !scene.depthCompare && !scene.colorBlend &&
                scene.vertexLayout.attributes.size() == 5,
            "default scene ABI unexpectedly overrides caller fixed-function state");
    const auto output = render::defaultOutputPipelinePolicy();
    require(output.vertexLayout.bindings.empty() && output.cullMode == rhi::CullMode::None &&
                output.depthTestEnabled == false && output.depthWriteEnabled == false &&
                !output.depthCompare && !output.colorBlend &&
                output.constantRanges[0].size == sizeof(render::PresentPushConstants),
            "full-screen output policy changed unrelated pass defaults");

    Fixture fixture;
    const render::MaterialPipelineInput input{fixture.program, &fixture.fragment, 1,
                                               fixture.layouts, 1};
    auto key = render::makePipelineVariantKey({0, 1}, asset::makeOpaqueMaterialState());
    key.shaderFeatures = static_cast<render::ShaderFeatureFlags>(1u << 5);
    rejects([&] { (void)render::MaterialPipelineCompiler{}.compile(input, key); }, "shader feature");
}

} // namespace

int main()
{
    try
    {
        testMaterialPolicy();
        testCanonicalVertexAbi();
        testAlphaClipContract();
        testInvalidAbi();
        testDefaultsAndUnsupportedFeatures();
        std::cout << "Material pipeline compiler tests passed (policy, mesh ABI, bindings, AlphaClip).\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
