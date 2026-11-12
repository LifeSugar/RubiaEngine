#include "asset/AssetManager.hpp"
#include "asset/AssetId.hpp"
#include "shader/HlslShaderCompiler.hpp"
#include "shader/SlangShaderCompiler.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace rubia;

namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
template<class Function>
void rejects(Function function, const char* diagnostic)
{
    try { function(); }
    catch (const std::exception& error)
    {
        require(std::string(error.what()).find(diagnostic) != std::string::npos,
                error.what());
        return;
    }
    throw std::runtime_error("Invalid shader unexpectedly compiled");
}
void run()
{
    // External UTF-8 paths, spaces and shell metacharacters must remain arguments.
    const auto directory = std::filesystem::temp_directory_path() /
        std::filesystem::u8path(u8"rubia 着色器 & compile " + asset::AssetId::generate().toString());
    require(std::filesystem::create_directory(directory), "Cannot create compiler fixture");
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
    } cleanup{directory};
    const auto sources = std::filesystem::path(PROJECT_SOURCE_DIR) / "assets/shaders";
    for (const auto* file : {"editor_unlit.slang", "editor_unlit.hlsl", "RenderData.hlsli"})
        std::filesystem::copy_file(sources / file, directory / file);

    const importer::shader::SlangShaderCompiler compiler;
    for (const auto* file : {"triangle.vert.hlsl", "triangle.frag.hlsl", "present.vert.hlsl", "present.frag.hlsl"})
    {
        std::cerr << "Compiling " << file << '\n';
        (void)compiler.compile(sources / file, std::string(file).find(".vert.")!=std::string::npos ? asset::ShaderStage::Vertex : asset::ShaderStage::Fragment);
    }
    const auto path = directory / "editor_unlit.slang";
    auto vertexFuture = std::async(std::launch::async, [&] {
        return compiler.compile(path, asset::ShaderStage::Vertex, "VSMain");
    });
    auto fragment = compiler.compile(path, asset::ShaderStage::Fragment, "PSMain");
    auto vertex = vertexFuture.get();
    require(vertex.entryPoint == "VSMain" && vertex.interface.entryPoint == "VSMain" &&
            vertex.interface.stage == asset::ShaderStage::Vertex &&
            fragment.entryPoint == "PSMain" && fragment.interface.entryPoint == "PSMain" &&
            fragment.interface.stage == asset::ShaderStage::Fragment,
            "Slang lost entry names or stages");
    require(vertex.spirv.at(1) == 0x00010300u, "Unexpected SPIR-V target version");
    require(vertex.interface.pushConstants.size() == 1 &&
            vertex.interface.pushConstants.front().byteSize == 12,
            "Slang push constants do not match the draw ABI");
    require(std::any_of(vertex.interface.inputs.begin(), vertex.interface.inputs.end(),
            [](const auto& input) { return input.location == 3 && input.type == asset::ShaderValueType::Float2; }),
            "Slang lost explicit vertex locations");
    asset::AssetManager assets;
    const auto vs = assets.createShader(std::move(vertex));
    const auto ps = assets.createShader(std::move(fragment));
    const auto program = assets.createShaderProgram({"Slang program", {vs, ps}});
    asset::MaterialTemplateAsset::CreateInfo info;
    info.program = program;
    info.textureSlots = {{"colorTexture", "colorTexture", "colorSampler"}};
    const auto materialTemplate = assets.createMaterialTemplate(info);
    const auto& layout = assets.materialTemplate(materialTemplate);
    require(layout.parameters().size() == 1 && layout.parameters()[0].name == "tint" &&
            layout.parameters()[0].byteOffset == 0 && layout.textureSlots().size() == 1 &&
            layout.textureSlots()[0].imageBinding.binding == 1 &&
            layout.textureSlots()[0].samplerBinding.binding == 2,
            "Slang material reflection lost names, offsets or bindings");

    // Source-level column-major matrices must not inherit SPIR-V RowMajor rejection.
    {
        std::ofstream matrix(directory / "matrix.slang");
        matrix << "struct Params { float4x4 transform; float4 tint; };\n"
                  "[[vk::binding(0,1)]] ConstantBuffer<Params> materialData;\n"
                  "float4 PSMain(float2 uv : TEXCOORD0) : SV_Target { return mul(materialData.transform,float4(uv,0,1))*materialData.tint; }\n";
    }
    const auto matrixPs = assets.createShader(compiler.compile(directory / "matrix.slang", asset::ShaderStage::Fragment, "PSMain"));
    asset::MaterialTemplateAsset::CreateInfo matrixInfo;
    matrixInfo.program = assets.createShaderProgram({"Matrix program", {vs, matrixPs}});
    const auto& matrixLayout = assets.materialTemplate(assets.createMaterialTemplate(matrixInfo));
    require(matrixLayout.parameters().size()==2 && matrixLayout.parameters()[0].type==asset::MaterialValueType::Matrix4 &&
            matrixLayout.parameters()[1].byteOffset==64, "Slang matrix layout was rejected or packed incorrectly");

    // HLSL compatibility entry also uses the unified Slang path.
    const importer::shader::HlslShaderCompiler hlsl;
    const auto hlslPath = directory / "editor_unlit.hlsl";
    const auto hlslVs = assets.createShader(hlsl.compile(hlslPath, asset::ShaderStage::Vertex, "VSMain"));
    const auto hlslPs = assets.createShader(hlsl.compile(hlslPath, asset::ShaderStage::Fragment, "PSMain"));
    require(bool(assets.createShaderProgram({"HLSL regression", {hlslVs, hlslPs}})),
            "HLSL compatibility compilation regressed");

    // Includes must be read again even if the entry source did not change.
    {
        std::ofstream include(directory / "color.slangh");
        include << "float4 color() { return float4(1, 0, 0, 1); }\n";
        std::ofstream source(directory / "include.slang");
        source << "#include \"color.slangh\"\nfloat4 shade() : SV_Target { return color(); }\n";
    }
    const auto before = compiler.compile(directory / "include.slang", asset::ShaderStage::Fragment, "shade");
    {
        std::ofstream include(directory / "color.slangh");
        include << "float4 color() { return float4(0, 1, 0, 1); }\n";
    }
    const auto after = compiler.compile(directory / "include.slang", asset::ShaderStage::Fragment, "shade");
    require(before.spirv != after.spirv, "Include edit did not change shader output");
    {
        std::ofstream source(directory / "compute.slang");
        source << "[[vk::binding(0, 0)]] RWStructuredBuffer<uint> result;\n"
                  "[numthreads(1, 1, 1)] void CSMain(uint3 id : SV_DispatchThreadID) { result[id.x] = id.x; }\n";
    }
    const auto compute = compiler.compile(directory / "compute.slang", asset::ShaderStage::Compute, "CSMain");
    require(compute.interface.stage == asset::ShaderStage::Compute && !compute.interface.resources.empty(),
            "Compute compilation or reflection failed");
    rejects([&] { (void)compiler.compile(path, asset::ShaderStage::Vertex, "MissingEntry"); }, "compilation failed");
    rejects([&] { (void)compiler.compile(path, asset::ShaderStage::Vertex, "1bad"); }, "identifier");
    rejects([&] { (void)compiler.compile(path, asset::ShaderStage::Vertex, "main;echo"); }, "identifier");
    rejects([&] { (void)compiler.compile(path, static_cast<asset::ShaderStage>(99), "VSMain"); }, "stage");
    {
        std::ofstream source(directory / "broken.slang");
        source << "this is not a shader;";
    }
    rejects([&] { (void)compiler.compile(directory / "broken.slang", asset::ShaderStage::Fragment); },
            "Slang compilation failed");
}
}

int main()
{
    try { run(); std::cout << "Shader compiler tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
