#pragma once

#include "EditorFwd.hpp"
#include "asset/AssetFwd.hpp"

#include <cstdint>
#include <string>

namespace rubia::editor
{

namespace widgets
{

[[nodiscard]] const char* displayName(
    const std::string& name,
    const char* fallback) noexcept;

void drawProperty(const char* label, const char* value);
void drawProperty(const char* label, uint32_t value);
void drawProperty(const char* label, float value);

// End a row only when beginPropertyRow returns true. Framed rows align labels
// with input controls and make those controls fill the value column.
[[nodiscard]] bool beginPropertyRow(const char* label, bool framed = false);
void endPropertyRow();

[[nodiscard]] bool drawReference(
    const char* label,
    const char* value,
    int id);

void drawTextureImage(
    render::ApplicationGuiRenderBridge& texturePreviews,
    asset::TextureAssetHandle handle,
    const asset::TextureAsset& texture,
    float maxWidth,
    float maxHeight);

} // namespace widgets

} // namespace rubia::editor
