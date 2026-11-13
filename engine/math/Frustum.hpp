#pragma once

#include "math/Aabb.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>

namespace rubia::math
{

/// Normalized plane whose non-negative half-space is inside the frustum.
struct Plane final
{
    glm::vec3 normal{0.0f};
    float distance = 0.0f;
};

/// Six clipping planes extracted from a logical zero-to-one view projection.
class Frustum final
{
public:
    static constexpr std::size_t PlaneCount = 6;

    /// Extracts normalized Left/Right/Bottom/Top/Near/Far planes.
    [[nodiscard]] static Frustum fromViewProjection(
        const glm::mat4& viewProjection);

    /// Tests a world AABB using center and projected-radius plane tests.
    [[nodiscard]] bool intersects(const Aabb& bounds) const noexcept;

    [[nodiscard]] const std::array<Plane, PlaneCount>& planes() const noexcept
    {
        return planes_;
    }

private:
    std::array<Plane, PlaneCount> planes_{};
};

} // namespace rubia::math
