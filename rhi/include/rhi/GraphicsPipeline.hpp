#pragma once
#include "rhi/Resources.hpp"
#include <array>

namespace rubia::rhi
{
enum class PrimitiveTopology { PointList, LineList, LineStrip, TriangleList, TriangleStrip, TriangleFan };
enum class PolygonMode { Fill, Line, Point };
enum class FrontFace { CounterClockwise, Clockwise };
enum class StencilOp { Keep, Zero, Replace, IncrementClamp, DecrementClamp, Invert, IncrementWrap, DecrementWrap };
enum class LogicOp { Clear, And, AndReverse, Copy, AndInverted, NoOp, Xor, Or, Nor, Equivalent, Invert, OrReverse, CopyInverted, OrInverted, Nand, Set };
enum class DynamicState { Viewport, Scissor, LineWidth, DepthBias, BlendConstants, DepthBounds, StencilCompareMask, StencilWriteMask, StencilReference };

struct InputAssemblyDesc
{
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    bool primitiveRestart = false;
};
struct RasterizationDesc
{
    bool depthClamp = false, discard = false;
    PolygonMode polygonMode = PolygonMode::Fill;
    CullMode cullMode = CullMode::Back;
    FrontFace frontFace = FrontFace::CounterClockwise;
    bool depthBias = false;
    float depthBiasConstant = 0, depthBiasClamp = 0, depthBiasSlope = 0, lineWidth = 1;
};
struct StencilFaceDesc
{
    StencilOp fail = StencilOp::Keep, pass = StencilOp::Keep, depthFail = StencilOp::Keep;
    CompareOp compare = CompareOp::Always;
    uint32_t compareMask = ~0u, writeMask = ~0u, reference = 0;
};
struct DepthStencilDesc
{
    bool depthTest = true, depthWrite = true;
    CompareOp depthCompare = CompareOp::Less;
    bool depthBounds = false, stencilTest = false;
    StencilFaceDesc front, back;
    float minDepthBounds = 0, maxDepthBounds = 1;
};
struct MultisampleDesc
{
    uint32_t samples = 1;
    bool sampleShading = false;
    float minSampleShading = 0;
    std::vector<uint32_t> sampleMask;
    bool alphaToCoverage = false, alphaToOne = false;
};
struct ColorBlendAttachmentDesc
{
    ColorBlendDesc blend;
    uint32_t writeMask = 0xf; // RGBA, one bit per component.
};
struct ColorBlendStateDesc
{
    bool logicOpEnabled = false;
    LogicOp logicOp = LogicOp::Copy;
    std::vector<ColorBlendAttachmentDesc> attachments = {{}};
    std::array<float, 4> constants{};
};
struct ViewportStateDesc
{
    std::vector<Viewport> viewports = {{}};
    std::vector<Rect> scissors = {{}};
};
struct ColorAttachmentSignature
{
    Format format = Format::Undefined;
    Format resolveFormat = Format::Undefined;
};
/// One subpass only. Dimensions, views, load/store and access states are not PSO state.
struct AttachmentSignature
{
    std::vector<ColorAttachmentSignature> colors;
    Format depthStencilFormat = Format::Undefined;
    uint32_t samples = 1;
};
/// Immutable backend compatibility contract. Content identity includes GPU
/// requirements not expressible as attachment formats alone (e.g. pass dependencies).
/// It contains no scene/asset semantics or public native handles.
class PipelineTargetResource : public Resource
{
public:
    virtual const AttachmentSignature& description() const noexcept = 0;
    virtual const std::vector<uint32_t>& compatibilityKey() const noexcept = 0;
};
using PipelineTargetRef = std::shared_ptr<const PipelineTargetResource>;
struct GraphicsPipelineDesc
{
    std::vector<ShaderRef> shaders;
    std::vector<BindingLayoutRef> bindingLayouts;
    std::vector<ConstantRangeDesc> constantRanges;
    std::vector<SpecializationConstant> specializations;
    VertexLayoutDesc vertexLayout;
    InputAssemblyDesc inputAssembly;
    RasterizationDesc rasterization;
    DepthStencilDesc depthStencil;
    MultisampleDesc multisample;
    ColorBlendStateDesc colorBlend;
    ViewportStateDesc viewport;
    std::vector<DynamicState> dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
    AttachmentSignature attachments;
    PipelineTargetRef target;
};
class PipelineResource : public Resource
{
public:
    virtual const GraphicsPipelineDesc& description() const noexcept = 0;
};
using PipelineRef = std::shared_ptr<const PipelineResource>;

/// Dedicated creation port so resource/upload implementations remain independent.
/// validate never creates GPU objects; create may run on the dedicated creation worker.
class PipelineDevice
{
public:
    virtual ~PipelineDevice() = default;
    virtual DeviceId deviceId() const noexcept = 0;
    virtual void validateGraphicsPipeline(const GraphicsPipelineDesc&) const = 0;
    virtual PipelineRef createGraphicsPipeline(const GraphicsPipelineDesc&) const = 0;
};
} // namespace rubia::rhi
