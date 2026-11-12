#include "shader/SlangShaderCompiler.hpp"
#include "shader/ExternalShaderCompiler.hpp"
#include "shader/ShaderInterfaceSignature.hpp"
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <slang-com-ptr.h>
#include <slang.h>
#include <stdexcept>

namespace rubia::importer::shader {
namespace {
using Kind = slang::TypeReflection::Kind;
using Scalar = slang::TypeReflection::ScalarType;
using Category = slang::ParameterCategory;
using Slang::ComPtr;
uint32_t narrow(size_t value) {
  if (value > UINT32_MAX)
    throw std::runtime_error("Slang layout exceeds supported size");
  return static_cast<uint32_t>(value);
}
asset::ShaderValueType valueType(slang::TypeLayoutReflection *type) {
  using V = asset::ShaderValueType;
  if (type->getKind() == Kind::Matrix)
    return type->getRowCount() == 4 && type->getColumnCount() == 4 &&
                   type->getScalarType() == Scalar::Float32
               ? V::Matrix4
               : V::Unknown;
  if (type->getKind() == Kind::Vector &&
      type->getScalarType() == Scalar::Float32) {
    switch (type->getElementCount()) {
    case 2:
      return V::Float2;
    case 3:
      return V::Float3;
    case 4:
      return V::Float4;
    default:
      return V::Unknown;
    }
  }
  if (type->getKind() != Kind::Scalar)
    return V::Unknown;
  switch (type->getScalarType()) {
  case Scalar::Float32:
    return V::Float;
  case Scalar::Int32:
    return V::Int;
  case Scalar::UInt32:
    return V::UInt;
  case Scalar::Bool:
    return V::Bool;
  default:
    return V::Unknown;
  }
}
uint64_t layoutHash(slang::TypeLayoutReflection *type) {
  uint64_t hash = 14695981039346656037ull;
  auto add = [&](uint64_t v) {
    for (int i = 0; i < 8; ++i) {
      hash ^= uint8_t(v >> (i * 8));
      hash *= 1099511628211ull;
    }
  };
  add(uint64_t(type->getKind()));
  add(type->getSize());
  add(type->getStride());
  add(uint64_t(type->getScalarType()));
  add(type->getRowCount());
  add(type->getColumnCount());
  if (type->getKind() == Kind::Matrix)
    add(type->getMatrixLayoutMode());
  if (type->getKind() == Kind::Array) {
    add(type->getElementCount());
    add(layoutHash(type->getElementTypeLayout()));
  }
  for (unsigned i = 0; i < type->getFieldCount(); ++i) {
    auto f = type->getFieldByIndex(i);
    add(f->getOffset());
    add(layoutHash(f->getTypeLayout()));
  }
  return hash;
}
std::vector<asset::ShaderBlockMemberDesc>
members(slang::TypeLayoutReflection *block) {
  std::vector<asset::ShaderBlockMemberDesc> result;
  for (unsigned i = 0; i < block->getFieldCount(); ++i) {
    auto field = block->getFieldByIndex(i);
    auto type = field->getTypeLayout();
    const bool matrix = type->getKind() == Kind::Matrix;
    result.push_back({field->getName() ? field->getName() : "", valueType(type),
                      narrow(field->getOffset()), narrow(type->getSize()),
                      type->isArray() ? narrow(type->getElementCount()) : 1,
                      matrix ? 16u : 0,
                      matrix && type->getMatrixLayoutMode() ==
                                    SLANG_MATRIX_LAYOUT_ROW_MAJOR});
  }
  return result;
}
void io(std::vector<asset::ShaderStageIoDesc> &result,
        slang::VariableLayoutReflection *var, Category category,
        uint32_t base = 0) {
  if (!var)
    return;
  auto type = var->getTypeLayout();
  const auto offset = narrow(var->getOffset(category));
  if (type->getKind() == Kind::Struct) {
    for (unsigned i = 0; i < type->getFieldCount(); ++i)
      io(result, type->getFieldByIndex(i), category, base + offset);
    return;
  }
  const char *semantic = var->getSemanticName();
  if (semantic && std::string(semantic).rfind("SV_", 0) == 0 &&
      std::string(semantic) != "SV_TARGET")
    return;
  if (type->getSize(category) == 0)
    return;
  result.push_back({var->getName() ? var->getName() : "result", base + offset,
                    valueType(type)});
}
void check(SlangResult result, slang::IBlob *diagnostics) {
  if (SLANG_FAILED(result))
    throw std::runtime_error(
        std::string("Slang compilation failed:\n") +
        (diagnostics ? std::string(static_cast<const char *>(
                                       diagnostics->getBufferPointer()),
                                   diagnostics->getBufferSize())
                     : "compiler operation failed"));
}
} // namespace
asset::ShaderAsset::CreateInfo
SlangShaderCompiler::compile(const std::filesystem::path &source,
                             asset::ShaderStage stage,
                             const std::string &entryPoint) const {
  if (entryPoint.empty() || (entryPoint[0] >= '0' && entryPoint[0] <= '9') ||
      entryPoint.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
          std::string::npos)
    throw std::invalid_argument("Shader entry point must be an identifier");
  SlangStage slangStage;
  switch (stage) {
  case asset::ShaderStage::Vertex:
    slangStage = SLANG_STAGE_VERTEX;
    break;
  case asset::ShaderStage::Fragment:
    slangStage = SLANG_STAGE_FRAGMENT;
    break;
  case asset::ShaderStage::Compute:
    slangStage = SLANG_STAGE_COMPUTE;
    break;
  default:
    throw std::invalid_argument("Unsupported shader stage");
  }
  const auto path = std::filesystem::canonical(source);
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw std::runtime_error("Cannot read Slang source");
  const std::string code((std::istreambuf_iterator<char>(stream)), {});
  // Slang SDK global-session state is serialized; each import owns its local
  // session.
  static std::mutex compilerMutex;
  const std::lock_guard<std::mutex> lock(compilerMutex);
  static ComPtr<slang::IGlobalSession> global = [] {
    ComPtr<slang::IGlobalSession> result;
    check(slang::createGlobalSession(result.writeRef()), nullptr);
    return result;
  }();
  slang::TargetDesc target{};
  target.format = SLANG_SPIRV;
  target.profile = global->findProfile("spirv_1_3");
  slang::CompilerOptionEntry options[2]{};
  options[0].name = slang::CompilerOptionName::VulkanUseEntryPointName;
  options[1].name = slang::CompilerOptionName::EmitSpirvDirectly;
  for (auto &option : options) {
    option.value.kind = slang::CompilerOptionValueKind::Int;
    option.value.intValue0 = 1;
  }
  const auto search = path.parent_path().u8string();
  const char *searches[] = {search.c_str()};
  slang::SessionDesc desc{};
  desc.targets = &target;
  desc.targetCount = 1;
  desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
  desc.searchPaths = searches;
  desc.searchPathCount = 1;
  desc.compilerOptionEntries = options;
  desc.compilerOptionEntryCount = 2;
  ComPtr<slang::ISession> session;
  check(global->createSession(desc, session.writeRef()), nullptr);
  ComPtr<slang::IBlob> diagnostics;
  auto module = session->loadModuleFromSourceString(
      "rubia_import", path.u8string().c_str(), code.c_str(),
      diagnostics.writeRef());
  if (!module)
    check(SLANG_FAIL, diagnostics);
  ComPtr<slang::IEntryPoint> entry;
  auto status = module->findAndCheckEntryPoint(
      entryPoint.c_str(), slangStage, entry.writeRef(), diagnostics.writeRef());
  check(status, diagnostics);
  slang::IComponentType *components[] = {module, entry};
  ComPtr<slang::IComponentType> composite, linked;
  status = session->createCompositeComponentType(
      components, 2, composite.writeRef(), diagnostics.writeRef());
  check(status, diagnostics);
  status = composite->link(linked.writeRef(), diagnostics.writeRef());
  check(status, diagnostics);
  auto layout = linked->getLayout(0, diagnostics.writeRef());
  if (!layout)
    check(SLANG_FAIL, diagnostics);
  ComPtr<slang::IMetadata> metadata;
  // Resource-free entry points need no use metadata (SDK 2026.1 codegen
  // workaround).
  if (layout->getParameterCount() != 0) {
    status = linked->getEntryPointMetadata(0, 0, metadata.writeRef(),
                                           diagnostics.writeRef());
    check(status, diagnostics);
  }
  auto result = detail::compileExternalShader(path, stage, entryPoint,
                                              detail::ShaderCompiler::Slang);
  auto &reflected = result.interface;
  reflected.stage = stage;
  reflected.entryPoint = entryPoint;
  for (unsigned i = 0; i < layout->getParameterCount(); ++i) {
    auto var = layout->getParameterByIndex(i);
    auto type = var->getTypeLayout();
    auto category = var->getCategory();
    const auto binding = var->getBindingIndex(), space = var->getBindingSpace();
    bool used = true;
    if (metadata && category == Category::DescriptorTableSlot &&
        SLANG_SUCCEEDED(metadata->isParameterLocationUsed(
            static_cast<SlangParameterCategory>(category), space, binding,
            used)) &&
        !used)
      continue;
    const std::string name = var->getName() ? var->getName() : "";
    if (category == Category::SpecializationConstant) {
      reflected.specializationConstants.push_back({binding, valueType(type)});
      continue;
    }
    if (category == Category::PushConstantBuffer) {
      auto element = type->getElementTypeLayout();
      reflected.pushConstants.push_back(
          {name, narrow(element->getSize()), stage, 0, members(element)});
      continue;
    }
    asset::ShaderResourceType resource;
    if (type->getKind() == Kind::ConstantBuffer)
      resource = asset::ShaderResourceType::UniformBuffer;
    else if (type->getKind() == Kind::SamplerState)
      resource = asset::ShaderResourceType::Sampler;
    else if (type->getKind() == Kind::Resource) {
      if ((type->getResourceShape() & SLANG_RESOURCE_BASE_SHAPE_MASK) ==
          SLANG_STRUCTURED_BUFFER)
        resource = asset::ShaderResourceType::StorageBuffer;
      else if (type->getResourceAccess() == SLANG_RESOURCE_ACCESS_READ)
        resource = asset::ShaderResourceType::SampledImage;
      else
        throw std::invalid_argument(
            "Slang reflection: unsupported resource access");
    } else
      throw std::invalid_argument(
          "Slang reflection: unsupported parameter kind: " + name);
    reflected.resources.push_back({name, space, binding, resource, 1, stage});
    if (resource == asset::ShaderResourceType::UniformBuffer ||
        resource == asset::ShaderResourceType::StorageBuffer) {
      auto element = type->getElementTypeLayout();
      reflected.parameterBlocks.push_back(
          {name, space, binding, narrow(element->getSize()), stage,
           members(element), layoutHash(element)});
    }
  }
  auto ep = layout->getEntryPointByIndex(0);
  for (unsigned i = 0; i < ep->getParameterCount(); ++i)
    io(reflected.inputs, ep->getParameterByIndex(i), Category::VaryingInput);
  io(reflected.outputs, ep->getResultVarLayout(), Category::VaryingOutput);
  reflected.signature = shaderInterfaceSignature(reflected);
  return result;
}
} // namespace rubia::importer::shader
