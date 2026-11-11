#pragma once
#include "rhi/GraphicsPipeline.hpp"
#include <vulkan/vulkan.h>
#include <stdexcept>

namespace rubia::rhi::vulkan::pipeline_detail
{
inline VkCompareOp toNative(CompareOp value)
{
    switch (value) {
    case CompareOp::Never: return VK_COMPARE_OP_NEVER;
    case CompareOp::Less: return VK_COMPARE_OP_LESS;
    case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
    }
    throw std::invalid_argument("unsupported pipeline CompareOp");
}
inline CompareOp fromNativeCompareOp(VkCompareOp value)
{
    switch (value) {
    case VK_COMPARE_OP_NEVER: return CompareOp::Never;
    case VK_COMPARE_OP_LESS: return CompareOp::Less;
    case VK_COMPARE_OP_EQUAL: return CompareOp::Equal;
    case VK_COMPARE_OP_LESS_OR_EQUAL: return CompareOp::LessEqual;
    case VK_COMPARE_OP_GREATER: return CompareOp::Greater;
    case VK_COMPARE_OP_NOT_EQUAL: return CompareOp::NotEqual;
    case VK_COMPARE_OP_GREATER_OR_EQUAL: return CompareOp::GreaterEqual;
    case VK_COMPARE_OP_ALWAYS: return CompareOp::Always;
    default: throw std::invalid_argument("native pipeline CompareOp is outside the RHI subset");
    }
}
inline VkCullModeFlags toNative(CullMode value)
{
    switch (value) {
    case CullMode::None: return VK_CULL_MODE_NONE;
    case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
    }
    throw std::invalid_argument("unsupported pipeline CullMode");
}
inline CullMode fromNativeCullMode(VkCullModeFlags value)
{
    switch (value) {
    case VK_CULL_MODE_NONE: return CullMode::None;
    case VK_CULL_MODE_FRONT_BIT: return CullMode::Front;
    case VK_CULL_MODE_BACK_BIT: return CullMode::Back;
    case VK_CULL_MODE_FRONT_AND_BACK: return CullMode::FrontAndBack;
    default: throw std::invalid_argument("native pipeline CullMode is outside the RHI subset");
    }
}
inline VkBlendFactor toNative(BlendFactor value)
{
    switch (value) {
    case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SourceAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSourceAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::SourceColor: return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::OneMinusSourceColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::DestinationColor: return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::OneMinusDestinationColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::DestinationAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::OneMinusDestinationAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case BlendFactor::ConstantColor: return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case BlendFactor::OneMinusConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case BlendFactor::ConstantAlpha: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case BlendFactor::OneMinusConstantAlpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    case BlendFactor::SourceAlphaSaturate: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case BlendFactor::Source1Color: return VK_BLEND_FACTOR_SRC1_COLOR;
    case BlendFactor::OneMinusSource1Color: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
    case BlendFactor::Source1Alpha: return VK_BLEND_FACTOR_SRC1_ALPHA;
    case BlendFactor::OneMinusSource1Alpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
    }
    throw std::invalid_argument("unsupported pipeline BlendFactor");
}
inline BlendFactor fromNativeBlendFactor(VkBlendFactor value)
{
    switch (value) {
    case VK_BLEND_FACTOR_ZERO: return BlendFactor::Zero;
    case VK_BLEND_FACTOR_ONE: return BlendFactor::One;
    case VK_BLEND_FACTOR_SRC_ALPHA: return BlendFactor::SourceAlpha;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: return BlendFactor::OneMinusSourceAlpha;
    case VK_BLEND_FACTOR_SRC_COLOR: return BlendFactor::SourceColor;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return BlendFactor::OneMinusSourceColor;
    case VK_BLEND_FACTOR_DST_COLOR: return BlendFactor::DestinationColor;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return BlendFactor::OneMinusDestinationColor;
    case VK_BLEND_FACTOR_DST_ALPHA: return BlendFactor::DestinationAlpha;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA: return BlendFactor::OneMinusDestinationAlpha;
    case VK_BLEND_FACTOR_CONSTANT_COLOR: return BlendFactor::ConstantColor;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR: return BlendFactor::OneMinusConstantColor;
    case VK_BLEND_FACTOR_CONSTANT_ALPHA: return BlendFactor::ConstantAlpha;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA: return BlendFactor::OneMinusConstantAlpha;
    case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE: return BlendFactor::SourceAlphaSaturate;
    case VK_BLEND_FACTOR_SRC1_COLOR: return BlendFactor::Source1Color;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR: return BlendFactor::OneMinusSource1Color;
    case VK_BLEND_FACTOR_SRC1_ALPHA: return BlendFactor::Source1Alpha;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA: return BlendFactor::OneMinusSource1Alpha;
    default: throw std::invalid_argument("native pipeline BlendFactor is outside the RHI subset");
    }
}
inline VkBlendOp toNative(BlendOp value)
{
    switch (value) {
    case BlendOp::Add: return VK_BLEND_OP_ADD;
    case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOp::Minimum: return VK_BLEND_OP_MIN;
    case BlendOp::Maximum: return VK_BLEND_OP_MAX;
    }
    throw std::invalid_argument("unsupported pipeline BlendOp");
}
inline BlendOp fromNativeBlendOp(VkBlendOp value)
{
    switch (value) {
    case VK_BLEND_OP_ADD: return BlendOp::Add;
    case VK_BLEND_OP_SUBTRACT: return BlendOp::Subtract;
    case VK_BLEND_OP_REVERSE_SUBTRACT: return BlendOp::ReverseSubtract;
    case VK_BLEND_OP_MIN: return BlendOp::Minimum;
    case VK_BLEND_OP_MAX: return BlendOp::Maximum;
    default: throw std::invalid_argument("native pipeline BlendOp is outside the RHI subset");
    }
}
inline VkFormat toNative(VertexFormat value)
{
    switch (value) {
    case VertexFormat::Float: return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
    throw std::invalid_argument("unsupported pipeline VertexFormat");
}
inline VertexFormat fromNativeVertexFormat(VkFormat value)
{
    switch (value) {
    case VK_FORMAT_R32_SFLOAT: return VertexFormat::Float;
    case VK_FORMAT_R32G32_SFLOAT: return VertexFormat::Float2;
    case VK_FORMAT_R32G32B32_SFLOAT: return VertexFormat::Float3;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return VertexFormat::Float4;
    default: throw std::invalid_argument("native pipeline VertexFormat is outside the RHI subset");
    }
}
inline VkVertexInputRate toNative(VertexInputRate value)
{
    switch (value) {
    case VertexInputRate::Vertex: return VK_VERTEX_INPUT_RATE_VERTEX;
    case VertexInputRate::Instance: return VK_VERTEX_INPUT_RATE_INSTANCE;
    }
    throw std::invalid_argument("unsupported pipeline VertexInputRate");
}
inline VertexInputRate fromNativeVertexInputRate(VkVertexInputRate value)
{
    switch (value) {
    case VK_VERTEX_INPUT_RATE_VERTEX: return VertexInputRate::Vertex;
    case VK_VERTEX_INPUT_RATE_INSTANCE: return VertexInputRate::Instance;
    default: throw std::invalid_argument("native pipeline VertexInputRate is outside the RHI subset");
    }
}
inline VkPrimitiveTopology toNative(PrimitiveTopology value)
{
    switch (value) {
    case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveTopology::TriangleFan: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    }
    throw std::invalid_argument("unsupported pipeline PrimitiveTopology");
}
inline PrimitiveTopology fromNativePrimitiveTopology(VkPrimitiveTopology value)
{
    switch (value) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: return PrimitiveTopology::PointList;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: return PrimitiveTopology::LineList;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: return PrimitiveTopology::LineStrip;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: return PrimitiveTopology::TriangleList;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return PrimitiveTopology::TriangleStrip;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN: return PrimitiveTopology::TriangleFan;
    default: throw std::invalid_argument("native pipeline PrimitiveTopology is outside the RHI subset");
    }
}
inline VkPolygonMode toNative(PolygonMode value)
{
    switch (value) {
    case PolygonMode::Fill: return VK_POLYGON_MODE_FILL;
    case PolygonMode::Line: return VK_POLYGON_MODE_LINE;
    case PolygonMode::Point: return VK_POLYGON_MODE_POINT;
    }
    throw std::invalid_argument("unsupported pipeline PolygonMode");
}
inline PolygonMode fromNativePolygonMode(VkPolygonMode value)
{
    switch (value) {
    case VK_POLYGON_MODE_FILL: return PolygonMode::Fill;
    case VK_POLYGON_MODE_LINE: return PolygonMode::Line;
    case VK_POLYGON_MODE_POINT: return PolygonMode::Point;
    default: throw std::invalid_argument("native pipeline PolygonMode is outside the RHI subset");
    }
}
inline VkFrontFace toNative(FrontFace value)
{
    switch (value) {
    case FrontFace::CounterClockwise: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    case FrontFace::Clockwise: return VK_FRONT_FACE_CLOCKWISE;
    }
    throw std::invalid_argument("unsupported pipeline FrontFace");
}
inline FrontFace fromNativeFrontFace(VkFrontFace value)
{
    switch (value) {
    case VK_FRONT_FACE_COUNTER_CLOCKWISE: return FrontFace::CounterClockwise;
    case VK_FRONT_FACE_CLOCKWISE: return FrontFace::Clockwise;
    default: throw std::invalid_argument("native pipeline FrontFace is outside the RHI subset");
    }
}
inline VkStencilOp toNative(StencilOp value)
{
    switch (value) {
    case StencilOp::Keep: return VK_STENCIL_OP_KEEP;
    case StencilOp::Zero: return VK_STENCIL_OP_ZERO;
    case StencilOp::Replace: return VK_STENCIL_OP_REPLACE;
    case StencilOp::IncrementClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case StencilOp::DecrementClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case StencilOp::Invert: return VK_STENCIL_OP_INVERT;
    case StencilOp::IncrementWrap: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case StencilOp::DecrementWrap: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    }
    throw std::invalid_argument("unsupported pipeline StencilOp");
}
inline StencilOp fromNativeStencilOp(VkStencilOp value)
{
    switch (value) {
    case VK_STENCIL_OP_KEEP: return StencilOp::Keep;
    case VK_STENCIL_OP_ZERO: return StencilOp::Zero;
    case VK_STENCIL_OP_REPLACE: return StencilOp::Replace;
    case VK_STENCIL_OP_INCREMENT_AND_CLAMP: return StencilOp::IncrementClamp;
    case VK_STENCIL_OP_DECREMENT_AND_CLAMP: return StencilOp::DecrementClamp;
    case VK_STENCIL_OP_INVERT: return StencilOp::Invert;
    case VK_STENCIL_OP_INCREMENT_AND_WRAP: return StencilOp::IncrementWrap;
    case VK_STENCIL_OP_DECREMENT_AND_WRAP: return StencilOp::DecrementWrap;
    default: throw std::invalid_argument("native pipeline StencilOp is outside the RHI subset");
    }
}
inline VkLogicOp toNative(LogicOp value)
{
    switch (value) {
    case LogicOp::Clear: return VK_LOGIC_OP_CLEAR;
    case LogicOp::And: return VK_LOGIC_OP_AND;
    case LogicOp::AndReverse: return VK_LOGIC_OP_AND_REVERSE;
    case LogicOp::Copy: return VK_LOGIC_OP_COPY;
    case LogicOp::AndInverted: return VK_LOGIC_OP_AND_INVERTED;
    case LogicOp::NoOp: return VK_LOGIC_OP_NO_OP;
    case LogicOp::Xor: return VK_LOGIC_OP_XOR;
    case LogicOp::Or: return VK_LOGIC_OP_OR;
    case LogicOp::Nor: return VK_LOGIC_OP_NOR;
    case LogicOp::Equivalent: return VK_LOGIC_OP_EQUIVALENT;
    case LogicOp::Invert: return VK_LOGIC_OP_INVERT;
    case LogicOp::OrReverse: return VK_LOGIC_OP_OR_REVERSE;
    case LogicOp::CopyInverted: return VK_LOGIC_OP_COPY_INVERTED;
    case LogicOp::OrInverted: return VK_LOGIC_OP_OR_INVERTED;
    case LogicOp::Nand: return VK_LOGIC_OP_NAND;
    case LogicOp::Set: return VK_LOGIC_OP_SET;
    }
    throw std::invalid_argument("unsupported pipeline LogicOp");
}
inline LogicOp fromNativeLogicOp(VkLogicOp value)
{
    switch (value) {
    case VK_LOGIC_OP_CLEAR: return LogicOp::Clear;
    case VK_LOGIC_OP_AND: return LogicOp::And;
    case VK_LOGIC_OP_AND_REVERSE: return LogicOp::AndReverse;
    case VK_LOGIC_OP_COPY: return LogicOp::Copy;
    case VK_LOGIC_OP_AND_INVERTED: return LogicOp::AndInverted;
    case VK_LOGIC_OP_NO_OP: return LogicOp::NoOp;
    case VK_LOGIC_OP_XOR: return LogicOp::Xor;
    case VK_LOGIC_OP_OR: return LogicOp::Or;
    case VK_LOGIC_OP_NOR: return LogicOp::Nor;
    case VK_LOGIC_OP_EQUIVALENT: return LogicOp::Equivalent;
    case VK_LOGIC_OP_INVERT: return LogicOp::Invert;
    case VK_LOGIC_OP_OR_REVERSE: return LogicOp::OrReverse;
    case VK_LOGIC_OP_COPY_INVERTED: return LogicOp::CopyInverted;
    case VK_LOGIC_OP_OR_INVERTED: return LogicOp::OrInverted;
    case VK_LOGIC_OP_NAND: return LogicOp::Nand;
    case VK_LOGIC_OP_SET: return LogicOp::Set;
    default: throw std::invalid_argument("native pipeline LogicOp is outside the RHI subset");
    }
}
inline VkDynamicState toNative(DynamicState value)
{
    switch (value) {
    case DynamicState::Viewport: return VK_DYNAMIC_STATE_VIEWPORT;
    case DynamicState::Scissor: return VK_DYNAMIC_STATE_SCISSOR;
    case DynamicState::LineWidth: return VK_DYNAMIC_STATE_LINE_WIDTH;
    case DynamicState::DepthBias: return VK_DYNAMIC_STATE_DEPTH_BIAS;
    case DynamicState::BlendConstants: return VK_DYNAMIC_STATE_BLEND_CONSTANTS;
    case DynamicState::DepthBounds: return VK_DYNAMIC_STATE_DEPTH_BOUNDS;
    case DynamicState::StencilCompareMask: return VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
    case DynamicState::StencilWriteMask: return VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
    case DynamicState::StencilReference: return VK_DYNAMIC_STATE_STENCIL_REFERENCE;
    }
    throw std::invalid_argument("unsupported pipeline DynamicState");
}
inline DynamicState fromNativeDynamicState(VkDynamicState value)
{
    switch (value) {
    case VK_DYNAMIC_STATE_VIEWPORT: return DynamicState::Viewport;
    case VK_DYNAMIC_STATE_SCISSOR: return DynamicState::Scissor;
    case VK_DYNAMIC_STATE_LINE_WIDTH: return DynamicState::LineWidth;
    case VK_DYNAMIC_STATE_DEPTH_BIAS: return DynamicState::DepthBias;
    case VK_DYNAMIC_STATE_BLEND_CONSTANTS: return DynamicState::BlendConstants;
    case VK_DYNAMIC_STATE_DEPTH_BOUNDS: return DynamicState::DepthBounds;
    case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK: return DynamicState::StencilCompareMask;
    case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK: return DynamicState::StencilWriteMask;
    case VK_DYNAMIC_STATE_STENCIL_REFERENCE: return DynamicState::StencilReference;
    default: throw std::invalid_argument("native pipeline DynamicState is outside the RHI subset");
    }
}
} // namespace rubia::rhi::vulkan::pipeline_detail
