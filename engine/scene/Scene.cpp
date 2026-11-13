#include "scene/Scene.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace rubia::scene
{
namespace
{

void validateHierarchy(const std::vector<SceneNode>& nodes)
{
    for (uint32_t index = 0;
         index < static_cast<uint32_t>(nodes.size());
         ++index)
    {
        const uint32_t parent = nodes[index].parent;
        if (parent != kInvalidSceneNodeIndex &&
            (parent >= nodes.size() || parent == index))
        {
            throw std::invalid_argument("scene node parent is invalid");
        }
    }

    for (uint32_t index = 0;
         index < static_cast<uint32_t>(nodes.size());
         ++index)
    {
        std::vector<bool> visited(nodes.size(), false);
        uint32_t ancestor = index;
        while (ancestor != kInvalidSceneNodeIndex)
        {
            if (visited[ancestor])
            {
                throw std::invalid_argument("scene hierarchy contains a cycle");
            }
            visited[ancestor] = true;
            ancestor = nodes[ancestor].parent;
        }
    }
}

} // namespace

Scene::Scene(CreateInfo createInfo)
{
    create(std::move(createInfo));
}

void Scene::create(CreateInfo createInfo)
{
    validateHierarchy(createInfo.nodes);
    name_ = std::move(createInfo.name);
    nodes_ = std::move(createInfo.nodes);
}

uint32_t Scene::addNode(SceneNode node)
{
    if (node.parent != kInvalidSceneNodeIndex && node.parent >= nodes_.size())
        throw std::invalid_argument("scene node parent is invalid");
    if (nodes_.size() >= kInvalidSceneNodeIndex)
        throw std::overflow_error("scene node capacity exceeded");
    const auto index = static_cast<uint32_t>(nodes_.size());
    nodes_.push_back(std::move(node));
    return index;
}

void Scene::setMaterialOverride(uint32_t nodeIndex, asset::MaterialAssetHandle material)
{
    nodes_.at(nodeIndex).materialOverride = material;
}

void Scene::reset() noexcept
{
    name_.clear();
    nodes_.clear();
}

void Scene::setLocalTransform(
    uint32_t nodeIndex,
    const glm::mat4& transform)
{
    if (nodeIndex >= nodes_.size())
    {
        throw std::out_of_range("scene node index is out of range");
    }
    nodes_[nodeIndex].localTransform = transform;
}

void Scene::setLayerMask(
    uint32_t nodeIndex,
    render::LayerMask layerMask)
{
    if (nodeIndex >= nodes_.size())
    {
        throw std::out_of_range("scene node index is out of range");
    }
    nodes_[nodeIndex].layerMask = layerMask;
}

void Scene::setBoundsCullingMode(
    uint32_t nodeIndex,
    render::BoundsCullingMode mode)
{
    if (nodeIndex >= nodes_.size())
    {
        throw std::out_of_range("scene node index is out of range");
    }
    nodes_[nodeIndex].boundsCullingMode = mode;
}

} // namespace rubia::scene
