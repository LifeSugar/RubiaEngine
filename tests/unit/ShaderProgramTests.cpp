#include "asset/AssetManager.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace rubia::asset;

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename Function> void rejects(Function function, const std::string &code)
{
    try
    {
        function();
    }
    catch (const AssetValidationError &error)
    {
        for (const auto &issue : error.report().issues())
        {
            if (issue.code == code)
            {
                return;
            }
        }
        throw std::runtime_error("wrong validation error: " + error.report().toString());
    }
    throw std::runtime_error("expected rejection: " + code);
}

// CPU builder fixtures intentionally provide reflection directly. Real SPIR-V
// reflection and the PBR program are exercised by asset-smoke-test.
ShaderAsset::CreateInfo shaderInfo(ShaderStage stage)
{
    ShaderAsset::CreateInfo info;
    info.name = stage == ShaderStage::Vertex ? "Test VS" : "Test PS";
    info.stage = stage;
    info.spirv = {0x07230203u, 0x00010000u, 0u, 1u, 0u};
    info.interface.stage = stage;
    info.interface.signature = 1;
    if (stage == ShaderStage::Vertex)
    {
        info.interface.inputs = {{"position", 0, ShaderValueType::Float3}};
        info.interface.outputs = {{"uv", 0, ShaderValueType::Float2}};
    }
    else
    {
        info.interface.inputs = {{"uv", 0, ShaderValueType::Float2}};
        info.interface.outputs = {{"color", 0, ShaderValueType::Float4}};
    }
    info.interface.resources = {{"material", 1, 7, ShaderResourceType::UniformBuffer, 1, stage}};
    ShaderParameterBlockDesc block;
    block.name = "material";
    block.set = 1;
    block.binding = 7;
    block.byteSize = 32; // Includes trailing padding, not the last member end.
    block.stage = stage;
    block.members = {{"roughness", ShaderValueType::Float, 16, 4}};
    info.interface.parameterBlocks = {block};
    info.interface.pushConstants = {{"draw", 8, stage, 4}};
    return info;
}

ShaderProgramAssetHandle program(AssetManager &assets, ShaderAsset::CreateInfo vs,
                                 ShaderAsset::CreateInfo ps)
{
    const auto vertex = assets.createShader(std::move(vs));
    const auto fragment = assets.createShader(std::move(ps));
    return assets.createShaderProgram({"Test", {vertex, fragment}});
}

void testMergeAndSignatures()
{
    AssetManager assets;
    const auto vs = assets.createShader(shaderInfo(ShaderStage::Vertex));
    const auto ps = assets.createShader(shaderInfo(ShaderStage::Fragment));
    const auto handle = assets.createShaderProgram({"Test", {ps, vs}});
    const auto code = assets.shaderProgram(handle).codeSignature();
    const auto layout = assets.shaderProgram(handle).layoutSignature();
    const auto interface = assets.shaderProgram(handle).interfaceSignature();
    const auto &merged = assets.shaderProgram(handle).interface();
    require(merged.bindings.size() == 1 && merged.bindings[0].binding == 7 &&
                merged.bindings[0].stages ==
                    (shaderStageMask(ShaderStage::Vertex) | shaderStageMask(ShaderStage::Fragment)),
            "shared bindings must merge stage visibility");
    require(merged.pushConstants.size() == 1 && merged.pushConstants[0].offset == 4 &&
                merged.pushConstants[0].size == 8,
            "push constant offsets must survive merging");
    const auto reordered = assets.createShaderProgram({"Renamed", {vs, ps}});
    require(code == assets.shaderProgram(reordered).codeSignature() &&
                layout == assets.shaderProgram(reordered).layoutSignature() &&
                interface == assets.shaderProgram(reordered).interfaceSignature(),
            "program ordering/name must not change signatures");

    auto changedCode = shaderInfo(ShaderStage::Fragment);
    changedCode.spirv.back() = 42;
    const auto changed = program(assets, shaderInfo(ShaderStage::Vertex), changedCode);
    require(code != assets.shaderProgram(changed).codeSignature() &&
                layout == assets.shaderProgram(changed).layoutSignature() &&
                interface == assets.shaderProgram(changed).interfaceSignature(),
            "code-only changes must preserve interface compatibility");

    auto changedPush = shaderInfo(ShaderStage::Fragment);
    changedPush.interface.pushConstants[0].offset = 8;
    const auto push = program(assets, shaderInfo(ShaderStage::Vertex), changedPush);
    require(layout != assets.shaderProgram(push).layoutSignature(),
            "push constant changes must invalidate layout signature");

    auto specialized = shaderInfo(ShaderStage::Fragment);
    specialized.interface.specializationConstants = {{1000, ShaderValueType::Bool}};
    const auto specializedProgram = program(assets, shaderInfo(ShaderStage::Vertex), specialized);
    require(interface != assets.shaderProgram(specializedProgram).interfaceSignature() &&
                layout == assets.shaderProgram(specializedProgram).layoutSignature(),
            "specialization contract must affect interface but not pipeline layout");
    auto pushMember = shaderInfo(ShaderStage::Fragment);
    pushMember.interface.pushConstants[0].members = {{"threshold", ShaderValueType::Float, 8, 4, 1}};
    const auto memberProgram = program(assets, shaderInfo(ShaderStage::Vertex), pushMember);
    require(interface != assets.shaderProgram(memberProgram).interfaceSignature() &&
                layout == assets.shaderProgram(memberProgram).layoutSignature(),
            "push member ABI must affect interface without changing an unchanged range");

    auto changedIo = shaderInfo(ShaderStage::Fragment);
    changedIo.interface.outputs[0].location = 1;
    const auto io = program(assets, shaderInfo(ShaderStage::Vertex), changedIo);
    require(interface != assets.shaderProgram(io).interfaceSignature(),
            "stage IO must participate in interface signature");

    rejects([&] { static_cast<void>(assets.createShaderProgram({"Bad", {vs, vs, ps}})); },
            "Program.DuplicateStage");
    rejects([&] { static_cast<void>(assets.createShaderProgram({"Bad", {vs}})); },
            "Program.InvalidStages");
    rejects([&] { static_cast<void>(assets.createShaderProgram({"Bad", {vs, {}}})); },
            "Program.InvalidShaderHandle");
    assets.reset();
    require(!assets.contains(handle), "reset must invalidate program handles");
}

void testConflicts()
{
    AssetManager assets;
    auto bad = shaderInfo(ShaderStage::Fragment);
    bad.interface.inputs[0].type = ShaderValueType::Float3;
    rejects([&] { static_cast<void>(program(assets, shaderInfo(ShaderStage::Vertex), bad)); },
            "Program.StageIoMismatch");
    bad = shaderInfo(ShaderStage::Fragment);
    bad.interface.resources[0].type = ShaderResourceType::StorageBuffer;
    rejects([&] { static_cast<void>(program(assets, shaderInfo(ShaderStage::Vertex), bad)); },
            "Program.ResourceMismatch");
    bad = shaderInfo(ShaderStage::Fragment);
    bad.interface.parameterBlocks[0].members[0].offset = 20;
    rejects([&] { static_cast<void>(program(assets, shaderInfo(ShaderStage::Vertex), bad)); },
            "Program.ResourceMismatch");
    bad = shaderInfo(ShaderStage::Fragment);
    bad.interface.parameterBlocks[0].layoutSignature = 17;
    rejects([&] { static_cast<void>(program(assets, shaderInfo(ShaderStage::Vertex), bad)); },
            "Program.ResourceMismatch");
    bad = shaderInfo(ShaderStage::Fragment);
    bad.interface.resources[0].arrayCount = 2;
    rejects([&] { static_cast<void>(program(assets, shaderInfo(ShaderStage::Vertex), bad)); },
            "Program.ResourceMismatch");
}

void testGeneratedMaterial()
{
    AssetManager assets;
    auto vs = shaderInfo(ShaderStage::Vertex);
    auto ps = shaderInfo(ShaderStage::Fragment);
    const auto original = program(assets, vs, ps);
    MaterialTemplateAsset::CreateInfo info;
    info.name = "Generated";
    info.program = original;
    const auto materialTemplate = assets.createMaterialTemplate(info);
    require(assets.materialTemplate(materialTemplate).parameterDataSize() == 32 &&
                assets.materialTemplate(materialTemplate).parameters()[0].byteOffset == 16 &&
                assets.materialTemplate(materialTemplate).parameterBlock().descriptor.binding == 7,
            "template must derive padded size, offset and binding");
    MaterialAsset::CreateInfo material;
    material.name = "Instance";
    material.materialTemplate = materialTemplate;
    material.parameters = {{"roughness", 0.75f}};
    const auto handle = assets.createMaterial(material);
    const auto programVersion = assets.version(original);
    const auto templateVersion = assets.version(materialTemplate);
    const auto materialVersion = assets.version(handle);
    require(programVersion.contentRevision == 1 && templateVersion.contentRevision == 1 &&
                materialVersion.contentRevision == 1 && assets.isCurrent(programVersion) &&
                assets.isCurrent(templateVersion) && assets.isCurrent(materialVersion),
            "program/template/material must expose content revisions");
    for (const auto shader : assets.shaderProgram(original).shaders())
    {
        require(assets.contentRevision(shader) == 1 && assets.isCurrent(assets.version(shader)),
                "shader must expose its own content revision");
    }
    float roughness = 0;
    std::memcpy(&roughness, assets.material(handle).parameterData().data() + 16, sizeof(float));
    require(roughness == 0.75f, "material must pack values at reflected offsets");

    for (auto *shader : {&vs, &ps})
    {
        shader->interface.parameterBlocks[0].byteSize = 48;
        shader->interface.parameterBlocks[0].members[0].offset = 32;
        shader->interface.resources[0].binding = 13;
        shader->interface.parameterBlocks[0].binding = 13;
    }
    info.program = program(assets, vs, ps);
    const auto movedTemplate = assets.createMaterialTemplate(info);
    material.materialTemplate = movedTemplate;
    const auto moved = assets.createMaterial(material);
    std::memcpy(&roughness, assets.material(moved).parameterData().data() + 32, sizeof(float));
    require(roughness == 0.75f && assets.material(moved).parameterData().size() == 48 &&
                assets.materialTemplate(movedTemplate).parameterBlock().descriptor.binding == 13 &&
                assets.materialTemplate(movedTemplate).schemaSignature() !=
                    assets.materialTemplate(materialTemplate).schemaSignature(),
            "shader layout changes must require no handwritten template updates");
    info.parameters = {{"missing", true}};
    rejects([&] { static_cast<void>(assets.createMaterialTemplate(info)); },
            "Template.InvalidParameterMetadata");
    info.parameters.clear();

    vs.interface.parameterBlocks[0].members[0].arrayCount = 2;
    ps.interface.parameterBlocks[0].members[0].arrayCount = 2;
    info.program = program(assets, vs, ps);
    rejects([&] { static_cast<void>(assets.createMaterialTemplate(info)); },
            "Template.UnsupportedParameter");
    require(assets.isCurrent(programVersion) && assets.isCurrent(templateVersion) &&
                assets.isCurrent(materialVersion),
            "failed creation or publishing another asset changed existing versions");
    assets.reset();
    require(!assets.isCurrent(programVersion) && !assets.isCurrent(templateVersion) &&
                !assets.isCurrent(materialVersion),
            "reset must invalidate program/template/material versions");
}

void testTextureMapping()
{
    AssetManager assets;
    auto vs = shaderInfo(ShaderStage::Vertex);
    auto ps = shaderInfo(ShaderStage::Fragment);
    ps.interface.resources.push_back(
        {"albedo", 1, 12, ShaderResourceType::SampledImage, 1, ShaderStage::Fragment});
    ps.interface.resources.push_back(
        {"linearSampler", 1, 21, ShaderResourceType::Sampler, 1, ShaderStage::Fragment});
    // Renderer-owned resource must not leak into the material set.
    vs.interface.resources.push_back(
        {"frameImage", 0, 9, ShaderResourceType::SampledImage, 1, ShaderStage::Vertex});
    MaterialTemplateAsset::CreateInfo info;
    info.name = "Textured";
    info.program = program(assets, vs, ps);
    info.textureSlots = {{"baseColor", "albedo", "linearSampler"}};
    const auto handle = assets.createMaterialTemplate(info);
    const auto &materialTemplate = assets.materialTemplate(handle);
    require(materialTemplate.bindings().size() == 3 &&
                materialTemplate.textureSlots()[0].imageBinding.binding == 12 &&
                materialTemplate.textureSlots()[0].samplerBinding.binding == 21,
            "texture bindings must be resolved by resource names");
    info.textureSlots[0].samplerResource = "missing";
    rejects([&] { static_cast<void>(assets.createMaterialTemplate(info)); },
            "Template.TextureResourceMissing");
    info.textureSlots.clear();
    rejects([&] { static_cast<void>(assets.createMaterialTemplate(info)); },
            "Template.UnmappedTextureResource");
}
} // namespace

int main()
{
    try
    {
        testMergeAndSignatures();
        testConflicts();
        testGeneratedMaterial();
        testTextureMapping();
        std::cout << "Shader program and generated material tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
