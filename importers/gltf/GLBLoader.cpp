#include "gltf/GLBLoader.hpp"

// ── Assimp 头文件仅在此 .cpp 中引入（pimpl） ────────────────────────────────
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/GltfMaterial.h>
#include <assimp/postprocess.h>
#include <assimp/matrix4x4.h>

#include <cstring>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <map>
#include <functional>
#include <stdexcept>

namespace rubia::importer::gltf
{

// ============================================================================
// pimpl 实现体
// ============================================================================
class GLBLoader::Impl {
public:
    Assimp::Importer importer;
};

// ============================================================================
// 构造 / 析构
// ============================================================================
GLBLoader::GLBLoader()
    : m_impl(std::make_unique<Impl>())
{}

GLBLoader::~GLBLoader() = default;

// ============================================================================
// load —— 主入口
// ============================================================================
std::unique_ptr<GLBModel> GLBLoader::load(const std::string& filePath)
{
    m_model = std::make_unique<GLBModel>();
    m_lastError.clear();

    // 通过 Assimp 读取文件（glTF2 / GLB 均由 Assimp 内置支持）
    const aiScene* scene = m_impl->importer.ReadFile(
        filePath,
        aiProcess_Triangulate
        | aiProcess_FlipUVs
        | aiProcess_GenNormals
        | aiProcess_CalcTangentSpace
        | aiProcess_LimitBoneWeights
    );

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        m_lastError = m_impl->importer.GetErrorString();
        return nullptr;
    }

    // 提取各项数据
    m_model->name = std::filesystem::u8path(filePath).filename().u8string();
    extractTextures(scene);
    extractMaterials(scene);
    extractMeshes(scene);
    extractSkins(scene);
    extractNode(scene->mRootNode, m_model->rootNode);
    extractAnimations(scene);

    return std::move(m_model);
}

// ============================================================================
// 工具函数 —— 类型转换
// ============================================================================
glm::mat4 GLBLoader::convertMatrix(const void* aiMatPtr)
{
    const auto& src = *static_cast<const aiMatrix4x4*>(aiMatPtr);
    // glm::mat4 列主序（与 glTF/OpenGL 一致），Assimp 行主序需转置
    return glm::mat4(
        src.a1, src.b1, src.c1, src.d1,
        src.a2, src.b2, src.c2, src.d2,
        src.a3, src.b3, src.c3, src.d3,
        src.a4, src.b4, src.c4, src.d4
    );
}

glm::vec3 GLBLoader::convertVec3(const void* aiVecPtr)
{
    const auto& src = *static_cast<const aiVector3D*>(aiVecPtr);
    return glm::vec3(src.x, src.y, src.z);
}

glm::vec4 GLBLoader::convertVec4(const void* aiColorPtr)
{
    const auto& src = *static_cast<const aiColor4D*>(aiColorPtr);
    return glm::vec4(src.r, src.g, src.b, src.a);
}

// ============================================================================
// extractMeshes
// ============================================================================
void GLBLoader::extractMeshes(const void* aiScenePtr)
{
    const auto* scene = static_cast<const aiScene*>(aiScenePtr);
    m_model->meshes.reserve(scene->mNumMeshes);

    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* src = scene->mMeshes[i];

        GLBMesh mesh;
        mesh.name = src->mName.C_Str();

        // 每个 aiMesh 对应一个 Primitive
        GLBPrimitive prim;
        prim.materialIndex = static_cast<int>(src->mMaterialIndex);

        // ── 顶点数据 ────────────────────────────────────────────────────
        prim.vertices.reserve(src->mNumVertices);
        for (unsigned int v = 0; v < src->mNumVertices; ++v) {
            GLBVertex vert;

            // Position
            vert.position = glm::vec3(src->mVertices[v].x, src->mVertices[v].y, src->mVertices[v].z);

            // Normal
            if (src->HasNormals()) {
                vert.normal = glm::vec3(src->mNormals[v].x, src->mNormals[v].y, src->mNormals[v].z);
            }

            // TexCoord 0
            if (src->HasTextureCoords(0)) {
                vert.texCoord = glm::vec2(src->mTextureCoords[0][v].x, src->mTextureCoords[0][v].y);
            }

            // TexCoord 1
            if (src->HasTextureCoords(1)) {
                vert.texCoord2 = glm::vec2(src->mTextureCoords[1][v].x, src->mTextureCoords[1][v].y);
            }

            // Tangent & Bitangent
            if (src->HasTangentsAndBitangents()) {
                const glm::vec3 tangent(
                    src->mTangents[v].x,
                    src->mTangents[v].y,
                    src->mTangents[v].z);
                const glm::vec3 bitangent(
                    src->mBitangents[v].x,
                    src->mBitangents[v].y,
                    src->mBitangents[v].z);
                const float handedness = glm::dot(
                    glm::cross(vert.normal, tangent),
                    bitangent) < 0.0f
                    ? -1.0f
                    : 1.0f;
                vert.tangent = glm::vec4(tangent, handedness);
            }

            // Vertex Color
            if (src->HasVertexColors(0)) {
                vert.color = glm::vec4(src->mColors[0][v].r, src->mColors[0][v].g,
                                       src->mColors[0][v].b, src->mColors[0][v].a);
            }

            prim.vertices.push_back(vert);
        }

        // ── 骨骼权重（统一放到最后一个 Primitive 的顶点上） ──────────────
        if (src->HasBones()) {
            // 初始化所有顶点的 boneIds / boneWeights
            // （因为 Assimp 以骨骼为中心存储权重，需翻转）
            for (auto& vert : prim.vertices) {
                vert.boneIds    = { -1, -1, -1, -1 };
                vert.boneWeights = { 0, 0, 0, 0 };
            }

            for (unsigned int b = 0; b < src->mNumBones; ++b) {
                const aiBone* bone = src->mBones[b];
                for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
                    const auto& bw = bone->mWeights[w];
                    GLBVertex& vert = prim.vertices[bw.mVertexId];

                    // 找到第一个空闲槽位
                    for (int slot = 0; slot < 4; ++slot) {
                        if (vert.boneIds[slot] < 0) {
                            vert.boneIds[slot]    = static_cast<int32_t>(b);
                            vert.boneWeights[slot] = bw.mWeight;
                            break;
                        }
                    }
                }
            }
        }

        // ── 索引 ────────────────────────────────────────────────────────
        prim.indices.reserve(src->mNumFaces * 3);  // 已经三角化
        for (unsigned int f = 0; f < src->mNumFaces; ++f) {
            const aiFace& face = src->mFaces[f];
            for (unsigned int idx = 0; idx < face.mNumIndices; ++idx) {
                prim.indices.push_back(face.mIndices[idx]);
            }
        }

        mesh.primitives.push_back(std::move(prim));
        m_model->meshes.push_back(std::move(mesh));
    }
}

// ============================================================================
// extractMaterials
// ============================================================================
void GLBLoader::extractMaterials(const void* aiScenePtr)
{
    const auto* scene = static_cast<const aiScene*>(aiScenePtr);
    m_model->materials.reserve(scene->mNumMaterials);

    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        const aiMaterial* src = scene->mMaterials[i];
        GLBMaterial mat;

        // 名称
        aiString aiName;
        if (src->Get(AI_MATKEY_NAME, aiName) == AI_SUCCESS) {
            mat.name = aiName.C_Str();
        }

        aiString alphaMode;
        if (src->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS)
            mat.alphaMode = alphaMode.C_Str();
        src->Get(AI_MATKEY_GLTF_ALPHACUTOFF, mat.alphaCutoff);
        int doubleSided = 0;
        if (src->Get(AI_MATKEY_TWOSIDED, doubleSided) == AI_SUCCESS)
            mat.doubleSided = doubleSided != 0;

        // ── PBR 因子 ────────────────────────────────────────────────────
        aiColor4D color;
        if (src->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS) {
            mat.baseColorFactor = glm::vec4(color.r, color.g, color.b, color.a);
        }
        float val;
        if (src->Get(AI_MATKEY_METALLIC_FACTOR, val) == AI_SUCCESS) {
            mat.metallicFactor = val;
        }
        if (src->Get(AI_MATKEY_ROUGHNESS_FACTOR, val) == AI_SUCCESS) {
            mat.roughnessFactor = val;
        }

        // ── 贴图索引 ────────────────────────────────────────────────────
        aiString texPath;

        // Base Color
        if (src->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS ||
            src->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
            mat.baseColorTextureIndex = findOrAddTexture(texPath.C_Str());
        }

        // Metallic-Roughness
        if (src->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &texPath) == AI_SUCCESS ||
            src->GetTexture(aiTextureType_UNKNOWN, 0, &texPath) == AI_SUCCESS) {
            mat.metallicRoughnessTextureIndex = findOrAddTexture(texPath.C_Str());
        }

        // Normal
        if (src->GetTexture(aiTextureType_NORMALS, 0, &texPath) == AI_SUCCESS) {
            mat.normalTextureIndex = findOrAddTexture(texPath.C_Str());
        }

        // Occlusion
        if (src->GetTexture(aiTextureType_LIGHTMAP, 0, &texPath) == AI_SUCCESS ||
            src->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &texPath) == AI_SUCCESS) {
            mat.occlusionTextureIndex = findOrAddTexture(texPath.C_Str());
        }

        // Emissive
        if (src->GetTexture(aiTextureType_EMISSIVE, 0, &texPath) == AI_SUCCESS) {
            mat.emissiveTextureIndex = findOrAddTexture(texPath.C_Str());
        }

        m_model->materials.push_back(std::move(mat));
    }
}

int GLBLoader::findOrAddTexture(const std::string& uri)
{
    if (uri.empty()) return -1;
    auto& textures = m_model->textures;
    for (size_t i = 0; i < textures.size(); ++i) {
        if (textures[i].uri == uri) return static_cast<int>(i);
    }
    textures.push_back({ uri, uri });
    return static_cast<int>(textures.size()) - 1;
}

// ============================================================================
// extractTextures —— 提取嵌入纹理信息
// ============================================================================
void GLBLoader::extractTextures(const void* aiScenePtr)
{
    const auto* scene = static_cast<const aiScene*>(aiScenePtr);
    for (unsigned int i = 0; i < scene->mNumTextures; ++i) {
        const aiTexture* src = scene->mTextures[i];
        GLBTexture tex;
        tex.uri = "*" + std::to_string(i);
        tex.name = src->mFilename.length > 0
            ? src->mFilename.C_Str()
            : tex.uri;
        tex.formatHint = src->achFormatHint;

        if (src->mHeight == 0)
        {
            tex.storage = GLBTextureStorage::EncodedBytes;
            tex.data.resize(src->mWidth);
            const auto* bytes =
                reinterpret_cast<const uint8_t*>(src->pcData);
            std::copy(bytes, bytes + src->mWidth, tex.data.begin());
        }
        else
        {
            const std::size_t pixelCount =
                static_cast<std::size_t>(src->mWidth) * src->mHeight;
            if (pixelCount >
                std::numeric_limits<std::size_t>::max() / 4)
            {
                throw std::overflow_error(
                    "embedded GLB texture dimensions are too large");
            }

            tex.storage = GLBTextureStorage::Rgba8Payload;
            tex.width = src->mWidth;
            tex.height = src->mHeight;
            tex.data.resize(pixelCount * 4);
            for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
            {
                const aiTexel& source = src->pcData[pixel];
                tex.data[pixel * 4 + 0] = source.r;
                tex.data[pixel * 4 + 1] = source.g;
                tex.data[pixel * 4 + 2] = source.b;
                tex.data[pixel * 4 + 3] = source.a;
            }
        }
        m_model->textures.push_back(std::move(tex));
    }
}

// ============================================================================
// extractNode —— 递归提取节点树
// ============================================================================
void GLBLoader::extractNode(const void* aiNodePtr, GLBNode& outNode)
{
    const auto* src = static_cast<const aiNode*>(aiNodePtr);

    outNode.name = src->mName.C_Str();
    outNode.localTransform = convertMatrix(&src->mTransformation);

    // Mesh 索引
    outNode.meshIndices.resize(src->mNumMeshes);
    for (unsigned int i = 0; i < src->mNumMeshes; ++i) {
        outNode.meshIndices[i] = static_cast<int>(src->mMeshes[i]);
    }

    // 递归子节点
    outNode.children.reserve(src->mNumChildren);
    for (unsigned int i = 0; i < src->mNumChildren; ++i) {
        GLBNode child;
        extractNode(src->mChildren[i], child);
        child.parentIndex = -1;  // parent 信息由调用方维护
        outNode.children.push_back(std::move(child));
    }
}

// ============================================================================
// extractSkins
// ============================================================================
void GLBLoader::extractSkins(const void* aiScenePtr)
{
    const auto* scene = static_cast<const aiScene*>(aiScenePtr);

    // Assimp 不直接暴露 Skin，我们用 mesh 中的 Bones 信息来构建
    // 收集所有涉及的骨骼节点名 → 逆绑定矩阵 映射
    std::map<std::string, glm::mat4> boneIBMs;
    int maxJointCount = 0;

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!mesh->HasBones()) continue;

        for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
            const aiBone* bone = mesh->mBones[b];
            std::string boneName = bone->mName.C_Str();
            if (boneIBMs.find(boneName) == boneIBMs.end()) {
                boneIBMs[boneName] = convertMatrix(&bone->mOffsetMatrix);
            }
        }
        maxJointCount = std::max(maxJointCount, static_cast<int>(mesh->mNumBones));
    }

    if (boneIBMs.empty()) return;

    // 构建 Skin
    GLBSkin skin;
    skin.name = "Skin0";

    // 在节点树中查找骨骼对应的节点索引
    // 简单的递归查找
    std::function<void(GLBNode&, std::map<std::string, int>&)> indexNodes;
    indexNodes = [&](GLBNode& node, std::map<std::string, int>& map) {
        map[node.name] = -1;  // placeholder
        for (auto& child : node.children) {
            indexNodes(child, map);
        }
    };

    // 这里我们保持简单：直接把骨骼名和 IBM 存储
    // 节点索引映射留给下游完成
    for (const auto& kv : boneIBMs) {
        skin.jointNodeIndices.push_back(-1);  // 需要下游通过名称匹配
        skin.inverseBindMatrices.push_back(kv.second);
    }

    // 存储骨骼名供下游匹配用 — 扩展 GLBSkin
    // 为简洁起见，此处只存矩阵，下游可通过名称自行对应节点索引
    m_model->skins.push_back(std::move(skin));
}

// ============================================================================
// extractAnimations
// ============================================================================
void GLBLoader::extractAnimations(const void* aiScenePtr)
{
    const auto* scene = static_cast<const aiScene*>(aiScenePtr);

    for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
        const aiAnimation* src = scene->mAnimations[a];
        GLBAnimation anim;
        anim.name = src->mName.C_Str();

        for (unsigned int c = 0; c < src->mNumChannels; ++c) {
            const aiNodeAnim* chan = src->mChannels[c];
            GLBAnimationChannel channel;

            // 节点名 → 索引映射交给下游
            channel.nodeIndex = -1;  // 下游按名称查找

            // ── 时间戳（以秒为单位） ────────────────────────────────────
            unsigned int keyCount = std::max({ chan->mNumPositionKeys,
                                               chan->mNumRotationKeys,
                                               chan->mNumScalingKeys });

            // 使用 Position 通道的时间
            channel.timestamps.reserve(chan->mNumPositionKeys);
            for (unsigned int k = 0; k < chan->mNumPositionKeys; ++k) {
                channel.timestamps.push_back(static_cast<float>(
                    chan->mPositionKeys[k].mTime / src->mTicksPerSecond));
            }

            // 若 Position 关键帧不足，用 Rotation 的时间补
            if (channel.timestamps.size() < keyCount) {
                channel.timestamps.clear();
                for (unsigned int k = 0; k < chan->mNumRotationKeys; ++k) {
                    channel.timestamps.push_back(static_cast<float>(
                        chan->mRotationKeys[k].mTime / src->mTicksPerSecond));
                }
            }

            // ── Translation keys ────────────────────────────────────────
            channel.vec3Values.reserve(chan->mNumPositionKeys);
            for (unsigned int k = 0; k < chan->mNumPositionKeys; ++k) {
                const auto& v = chan->mPositionKeys[k].mValue;
                channel.vec3Values.push_back(glm::vec3(v.x, v.y, v.z));
            }

            // ── Rotation keys (quaternion) ──────────────────────────────
            channel.quatValues.reserve(chan->mNumRotationKeys);
            for (unsigned int k = 0; k < chan->mNumRotationKeys; ++k) {
                const auto& q = chan->mRotationKeys[k].mValue;
                channel.quatValues.push_back(glm::quat(q.w, q.x, q.y, q.z));
            }

            // ── Scale keys ──────────────────────────────────────────────
            // Scale 与 Translation 共用 vec3Values 不合适，我们简化处理：
            // 将 Scale 存储在 weightValues 中作为备用通道
            // 实际使用中，下游可判断 path 类型来解析
            if (chan->mNumScalingKeys > 0) {
                // 这里仅存储 scale 信息作为额外通道
                // 创建另一个 channel 来存 scale
                GLBAnimationChannel scaleChannel;
                scaleChannel.nodeIndex = -1;
                scaleChannel.path = GLBAnimationPath::Scale;
                scaleChannel.timestamps.reserve(chan->mNumScalingKeys);
                scaleChannel.vec3Values.reserve(chan->mNumScalingKeys);
                for (unsigned int k = 0; k < chan->mNumScalingKeys; ++k) {
                    scaleChannel.timestamps.push_back(static_cast<float>(
                        chan->mScalingKeys[k].mTime / src->mTicksPerSecond));
                    const auto& s = chan->mScalingKeys[k].mValue;
                    scaleChannel.vec3Values.push_back(glm::vec3(s.x, s.y, s.z));
                }
                // 将 Rotation 也存为单独通道
                GLBAnimationChannel rotChannel;
                rotChannel.nodeIndex = -1;
                rotChannel.path = GLBAnimationPath::Rotation;
                rotChannel.timestamps = channel.timestamps;
                rotChannel.quatValues = channel.quatValues;

                // 原 channel 存 Translation
                channel.path = GLBAnimationPath::Translation;
                channel.quatValues.clear();

                anim.channels.push_back(std::move(channel));
                anim.channels.push_back(std::move(rotChannel));
                anim.channels.push_back(std::move(scaleChannel));
            } else {
                // 只有 Position + Rotation
                channel.path = GLBAnimationPath::Translation;
                anim.channels.push_back(std::move(channel));
            }
        }

        m_model->animations.push_back(std::move(anim));
    }
}

} // namespace rubia::importer::gltf
