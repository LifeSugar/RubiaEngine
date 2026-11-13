#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rubia::rhi::vulkan::detail
{
// Field encoding, never raw structs: padding, pointers and handles are not keys.
struct PipelineKeyData
{
    std::vector<uint32_t> words;
    void add(uint32_t value)
    {
        words.push_back(value);
    }
    void count(size_t value)
    {
        const auto wide = static_cast<uint64_t>(value);
        add(static_cast<uint32_t>(wide));
        add(static_cast<uint32_t>(wide >> 32));
    }
    void real(float value)
    {
        uint32_t bits = 0;
        if (value != 0.0f)
            std::memcpy(&bits, &value, sizeof(bits));
        add(bits); // Normalize signed zero.
    }
    void bytes(const void *source, size_t size)
    {
        count(size);
        const auto *data = static_cast<const unsigned char *>(source);
        for (size_t i = 0; i < size; ++i)
            add(data[i]);
    }
    void string(const std::string &value)
    {
        bytes(value.data(), value.size());
    }
    void append(const std::vector<uint32_t> &value)
    {
        count(value.size());
        words.insert(words.end(), value.begin(), value.end());
    }
};
} // namespace rubia::rhi::vulkan::detail
