#include "render/Camera.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <limits>

namespace rubia::render
{

Camera::Camera()
    : m_position(0.0f, 0.0f, 0.0f)
    , m_rotation(0.0f, 0.0f, 0.0f)
    , m_config()
    , m_viewMatrix(1.0f)
    , m_projectionMatrix(1.0f)
    , m_viewProjectionMatrix(1.0f)
    , m_isViewDirty(true)
    , m_isProjectionDirty(true)
    , m_viewId(RenderViewId::generate())
{
}

void Camera::setConfig(const Config &config)
{
    if (m_config.fov != config.fov ||
        m_config.aspectRatio != config.aspectRatio ||
        m_config.nearPlane != config.nearPlane ||
        m_config.farPlane != config.farPlane)
    {
        m_config = config;
        m_isProjectionDirty = true;
        markGpuDataChanged();
    }
}

const Camera::Config &Camera::getConfig() const
{
    return this -> m_config;
}

void Camera::setPosition(const glm::vec3 &position)
{
    if (m_position != position)
    {
        m_position = position;
        m_isViewDirty = true;
        markGpuDataChanged();
    }
}

const glm::vec3 &Camera::getPosition() const
{
    return m_position;
}

void Camera::setRotation(const glm::vec3 &rotation)
{
    if (m_rotation != rotation)
    {
        m_rotation = rotation;
        m_isViewDirty = true;
        markGpuDataChanged();
    }
}

const glm::vec3 &Camera::getRotation() const
{
    return m_rotation;
}

void Camera::setCullingMask(
    LayerMask cullingMask) noexcept
{
    m_cullingMask = cullingMask;
}

LayerMask Camera::getCullingMask() const noexcept
{
    return m_cullingMask;
}

void Camera::setCullingFlags(
    CullingFlags cullingFlags) noexcept
{
    m_cullingFlags = cullingFlags;
}

CullingFlags Camera::getCullingFlags() const noexcept
{
    return m_cullingFlags;
}

const glm::mat4 &Camera::getViewMatrix() const
{
    if (m_isViewDirty)
    {
        recalculateViewMatrix();
    }
    return m_viewMatrix;
}

const glm::mat4 &Camera::getProjectionMatrix() const
{
    if (m_isProjectionDirty)
    {
        recalculateProjectionMatrix();
    }
    return m_projectionMatrix;
}

const glm::mat4 &Camera::getViewProjectionMatrix() const
{
    if (m_isViewDirty)
    {
        recalculateViewMatrix();
    }
    if (m_isProjectionDirty)
    {
        recalculateProjectionMatrix();
    }
    m_viewProjectionMatrix = m_projectionMatrix * m_viewMatrix;
    return m_viewProjectionMatrix;
}

CameraGpuData Camera::getGpuData() const
{
    CameraGpuData data{};
    data.viewProjection = getViewProjectionMatrix();
    data.worldPosition = glm::vec4(m_position, 1.0f);
    return data;
}

RenderView Camera::makeRenderView() const
{
    RenderView view{};
    view.id = m_viewId;
    view.gpuDataRevision = m_gpuDataRevision;
    view.viewMatrix = getViewMatrix();
    view.projectionMatrix = getProjectionMatrix();
    view.viewProjectionMatrix = getViewProjectionMatrix();
    view.worldPosition = m_position;
    view.gpuData.viewProjection = view.viewProjectionMatrix;
    view.gpuData.worldPosition = glm::vec4(m_position, 1.0f);
    view.cullingMask = m_cullingMask;
    view.cullingFlags = m_cullingFlags;
    return view;
}

void Camera::setAspect(float aspect)
{
    if (m_config.aspectRatio != aspect)
    {
        m_config.aspectRatio = aspect;
        m_isProjectionDirty = true;
        markGpuDataChanged();
    }
}

glm::vec3 Camera::getForwardVector() const
{
    glm::vec3 eulerRad = glm::radians(m_rotation);
    glm::quat q = glm::quat(eulerRad);
    glm::vec3 forward = q * glm::vec3(0.0f, 0.0f, -1.0f);
    return glm::normalize(forward);
}

glm::vec3 Camera::getRightVector() const
{
    glm::vec3 eulerRad = glm::radians(m_rotation);
    glm::quat q = glm::quat(eulerRad);
    glm::vec3 right = q * glm::vec3(1.0f, 0.0f, 0.0f);
    return glm::normalize(right);
}

glm::vec3 Camera::getUpVector() const
{
    glm::vec3 eulerRad = glm::radians(m_rotation);
    glm::quat q = glm::quat(eulerRad);
    glm::vec3 up = q * glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(up);
}

void Camera::Update()
{
    if (m_isViewDirty)
    {
        recalculateViewMatrix();
    }
    if (m_isProjectionDirty)
    {
        recalculateProjectionMatrix();
    }
}

void Camera::markGpuDataChanged()
{
    if (m_gpuDataRevision == std::numeric_limits<uint64_t>::max())
    {
        // Rotate the identity as well so a wrapped revision cannot match a
        // snapshot cached for this Camera's previous revision cycle.
        m_viewId = RenderViewId::generate();
        m_gpuDataRevision = 1;
    }
    else
    {
        ++m_gpuDataRevision;
    }
}

void Camera::recalculateViewMatrix() const
{
    glm::vec3 forward = getForwardVector();
    glm::vec3 up = getUpVector();
    m_viewMatrix = glm::lookAt(m_position, m_position + forward, up);
    m_isViewDirty = false;
}

void Camera::recalculateProjectionMatrix() const
{
    m_projectionMatrix = glm::perspective(
        glm::radians(m_config.fov),
        m_config.aspectRatio,
        m_config.nearPlane,
        m_config.farPlane
    );
    m_isProjectionDirty = false;
}

} // namespace rubia::render

