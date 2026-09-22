#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace rubia::importer::gltf
{

// ============================================================================
// GLB / glTF 下游数据结构 —— 基于 GLM 数学库
// ============================================================================

// ── 顶点 ────────────────────────────────────────────────────────────────────
// Import-domain data kept independent from the renderer's GPU vertex format.
// This importer-owned type is intentionally separate from the core Asset layer.
struct GLBVertex {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f);
    glm::vec2 texCoord = glm::vec2(0.0f);
    /// xyz is the tangent direction; w is the bitangent handedness.
    glm::vec4 tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    glm::vec2 texCoord2 = glm::vec2(0.0f);
    glm::vec4 color = glm::vec4(1.0f);

    std::array<int32_t, 4> boneIds = { -1, -1, -1, -1 };
    std::array<float, 4> boneWeights = { 0.0f, 0.0f, 0.0f, 0.0f };
};

// ── 网格图元（Primitive） ───────────────────────────────────────────────────
struct GLBPrimitive {
    std::vector<GLBVertex> vertices;
    std::vector<uint32_t>  indices;
    int                    materialIndex = -1;   // 指向 GLBModel::materials
};

// ── 网格 ────────────────────────────────────────────────────────────────────
struct GLBMesh {
    std::string             name;
    std::vector<GLBPrimitive> primitives;
};

// ── 纹理信息 ────────────────────────────────────────────────────────────────
enum class GLBTextureStorage {
    ExternalUri,
    EncodedBytes,
    Rgba8Payload
};

struct GLBTexture {
    std::string uri;          // 外部图片路径 或 data URI
    std::string name;
    GLBTextureStorage storage = GLBTextureStorage::ExternalUri;
    std::string formatHint;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> data;
};

// ── PBR 材质（兼容 glTF 2.0 metallic-roughness 工作流） ────────────────────
struct GLBMaterial {
    std::string name;
    std::string alphaMode = "OPAQUE";
    float alphaCutoff = 0.5f;
    bool doubleSided = false;

    // PBR 因子
    glm::vec4 baseColorFactor = glm::vec4(1);
    float     metallicFactor  = 1.0f;
    float     roughnessFactor = 1.0f;
    glm::vec3 emissiveFactor  = glm::vec3(0);

    // 贴图索引（-1 表示无贴图）
    int baseColorTextureIndex         = -1;
    int metallicRoughnessTextureIndex = -1;
    int normalTextureIndex            = -1;
    int occlusionTextureIndex         = -1;
    int emissiveTextureIndex          = -1;
};

// ── 场景节点（树形结构） ────────────────────────────────────────────────────
struct GLBNode {
    std::string name;
    glm::mat4   localTransform = glm::mat4(1);  // 相对于父节点的变换

    std::vector<int> meshIndices;        // 指向 GLBModel::meshes
    std::vector<int> skinIndex;          // 指向 GLBModel::skins（若有）

    std::vector<GLBNode> children;

    // 快速查找辅助
    int parentIndex = -1;                // 父节点索引，根节点为 -1
};

// ── 骨骼蒙皮 ────────────────────────────────────────────────────────────────
struct GLBSkin {
    std::string          name;
    std::vector<glm::mat4> inverseBindMatrices;  // 逆绑定矩阵
    std::vector<int>     jointNodeIndices;       // 指向节点索引
    int                  skeletonRoot = -1;      // 骨架根节点索引
};

// ── 动画通道 ────────────────────────────────────────────────────────────────
enum class GLBAnimationPath {
    Translation,
    Rotation,
    Scale,
    Weights
};

struct GLBAnimationChannel {
    int                   nodeIndex;      // 目标节点索引
    GLBAnimationPath      path;
    std::vector<float>    timestamps;     // 关键帧时间（秒）
    std::vector<glm::vec3> vec3Values;    // Translation / Scale
    std::vector<glm::quat> quatValues;    // Rotation (quaternion)
    std::vector<float>    weightValues;   // Morph weights
};

// ── 动画 ────────────────────────────────────────────────────────────────────
struct GLBAnimation {
    std::string                    name;
    std::vector<GLBAnimationChannel> channels;
};

// ── 顶层模型容器 ────────────────────────────────────────────────────────────
struct GLBModel {
    std::string name;

    std::vector<GLBMesh>      meshes;
    std::vector<GLBMaterial>  materials;
    std::vector<GLBTexture>   textures;
    GLBNode                   rootNode;
    std::vector<GLBSkin>      skins;
    std::vector<GLBAnimation> animations;
};

} // namespace rubia::importer::gltf
