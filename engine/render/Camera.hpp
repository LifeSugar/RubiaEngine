#pragma once
#include "render/RenderView.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

namespace rubia::render
{

class Camera
{
public:
    struct Config
    {
        float fov = 45.0f;
        float aspectRatio = 16.0f/9.0f;
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
    };

public:
    Camera();
    ~Camera() = default;

    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;

    void setConfig(const Config &config);
    const Config &getConfig() const;

    void setPosition(const glm::vec3 &position);
    const glm::vec3 &getPosition() const;

    void setRotation(const glm::vec3 &rotation);
    const glm::vec3 &getRotation() const;

    /// Selects the scene layers visible from this camera.
    void setCullingMask(LayerMask cullingMask) noexcept;
    [[nodiscard]] LayerMask getCullingMask() const noexcept;
    void setCullingFlags(CullingFlags cullingFlags) noexcept;
    [[nodiscard]] CullingFlags getCullingFlags() const noexcept;

    const glm::mat4 &getViewMatrix() const;
    /// Logical Y-up, zero-to-one depth projection; backend correction is
    /// applied only when Renderer uploads camera data to the GPU.
    const glm::mat4 &getProjectionMatrix() const;
    const glm::mat4 &getViewProjectionMatrix() const;
    [[nodiscard]] CameraGpuData getGpuData() const;
    /// Creates a consistent immutable snapshot for one render flow.
    [[nodiscard]] RenderView makeRenderView() const;
    [[nodiscard]] RenderViewId viewId() const noexcept
    {
        return m_viewId;
    }
    [[nodiscard]] uint64_t gpuDataRevision() const noexcept
    {
        return m_gpuDataRevision;
    }

    void setAspect(float aspect);

    glm::vec3 getForwardVector() const;
    glm::vec3 getRightVector() const;
    glm::vec3 getUpVector() const;

    void Update();

private:
    glm::vec3 m_position;
    glm::vec3 m_rotation;
    Config m_config;
    LayerMask m_cullingMask = LayerMask::all();
    CullingFlags m_cullingFlags = CullingFlags::All;

    mutable glm::mat4 m_viewMatrix;
    mutable glm::mat4 m_projectionMatrix;
    mutable glm::mat4 m_viewProjectionMatrix;

    mutable bool m_isViewDirty = true;
    mutable bool m_isProjectionDirty = true;
    RenderViewId m_viewId;
    uint64_t m_gpuDataRevision = 1;

private:
    void markGpuDataChanged();
    void recalculateViewMatrix() const;
    void recalculateProjectionMatrix() const;
};

} // namespace rubia::render
