#pragma once
#include <cstdint>

namespace rubia::render
{
// Required properties, not mutually exclusive memory classes. Extra properties
// in the actual allocation are allowed; unsupported combinations do not fall back.
enum class MaterialParameterMemory : uint8_t
{
    DeviceLocal = 1 << 0,
    HostVisible = 1 << 1
};
constexpr MaterialParameterMemory operator|(MaterialParameterMemory a, MaterialParameterMemory b)
{
    return static_cast<MaterialParameterMemory>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr bool hasMemoryProperty(MaterialParameterMemory value, MaterialParameterMemory property)
{
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(property)) == static_cast<uint8_t>(property);
}
constexpr bool validMaterialParameterMemory(MaterialParameterMemory value)
{
    const auto bits = static_cast<uint8_t>(value);
    return bits != 0 && (bits & ~uint8_t{3}) == 0;
}
struct MaterialPreparationOptions
{
    // DeviceLocal alone always stages. Including HostVisible selects direct CPU
    // writes, even if the allocation also has DeviceLocal. Fixed for the session.
    MaterialParameterMemory parameterMemory = MaterialParameterMemory::DeviceLocal;
};
struct ResourcePreparationOptions
{
    MaterialPreparationOptions material;
};
} // namespace rubia::render
