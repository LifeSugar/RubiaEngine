#include "shader/ShaderInterfaceSignature.hpp"
#include <algorithm>
#include <tuple>
namespace rubia::importer::shader {
void hashBytes(uint64_t &hash, const void *data, std::size_t size) noexcept {
  constexpr uint64_t kFnvPrime = UINT64_C(1099511628211);
  const auto *bytes = static_cast<const unsigned char *>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= kFnvPrime;
  }
}

template <typename Value>
void hashValue(uint64_t &hash, const Value &value) noexcept {
  hashBytes(hash, &value, sizeof(value));
}

void hashString(uint64_t &hash, const std::string &value) noexcept {
  hashBytes(hash, value.data(), value.size());
  const unsigned char terminator = 0;
  hashBytes(hash, &terminator, 1);
}

uint64_t shaderInterfaceSignature(const asset::ShaderInterface &interface) {
  uint64_t hash = UINT64_C(14695981039346656037);
  hashValue(hash, interface.stage);
  hashString(hash, interface.entryPoint);

  std::vector<asset::ShaderResourceDesc> resources = interface.resources;
  std::sort(resources.begin(), resources.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.set, left.binding, left.type, left.name) <
                     std::tie(right.set, right.binding, right.type, right.name);
            });
  for (const asset::ShaderResourceDesc &resource : resources) {
    hashString(hash, resource.name);
    hashValue(hash, resource.set);
    hashValue(hash, resource.binding);
    hashValue(hash, resource.type);
    hashValue(hash, resource.arrayCount);
  }

  std::vector<asset::ShaderParameterBlockDesc> blocks =
      interface.parameterBlocks;
  std::sort(blocks.begin(), blocks.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.set, left.binding, left.name) <
                     std::tie(right.set, right.binding, right.name);
            });
  for (const asset::ShaderParameterBlockDesc &block : blocks) {
    hashString(hash, block.name);
    hashValue(hash, block.set);
    hashValue(hash, block.binding);
    hashValue(hash, block.byteSize);
    hashValue(hash, block.layoutSignature);
    for (const asset::ShaderBlockMemberDesc &member : block.members) {
      hashString(hash, member.name);
      hashValue(hash, member.type);
      hashValue(hash, member.offset);
      hashValue(hash, member.size);
      hashValue(hash, member.arrayCount);
      hashValue(hash, member.matrixStride);
      hashValue(hash, member.rowMajor);
    }
  }
  const auto hashIo = [&](std::vector<asset::ShaderStageIoDesc> io) {
    std::sort(io.begin(), io.end(), [](const auto &a, const auto &b) {
      return a.location < b.location;
    });
    hashValue(hash, static_cast<uint64_t>(io.size()));
    for (const auto &item : io) {
      hashValue(hash, item.location);
      hashValue(hash, item.type);
    }
  };
  hashIo(interface.inputs);
  hashIo(interface.outputs);
  auto pushes = interface.pushConstants;
  std::sort(pushes.begin(), pushes.end(), [](const auto &a, const auto &b) {
    return std::tie(a.offset, a.byteSize) < std::tie(b.offset, b.byteSize);
  });
  for (const auto &push : pushes) {
    hashValue(hash, push.offset);
    hashValue(hash, push.byteSize);
    hashValue(hash, push.stage);
    for (const auto &member : push.members) {
      hashValue(hash, member.offset);
      hashValue(hash, member.type);
      hashValue(hash, member.size);
    }
  }
  auto constants = interface.specializationConstants;
  std::sort(
      constants.begin(), constants.end(),
      [](const auto &a, const auto &b) { return a.constantId < b.constantId; });
  for (const auto &constant : constants) {
    hashValue(hash, constant.constantId);
    hashValue(hash, constant.type);
  }
  return hash;
}

} // namespace rubia::importer::shader
