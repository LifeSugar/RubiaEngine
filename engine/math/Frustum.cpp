#include "math/Frustum.hpp"

#include <cmath>
#include <stdexcept>

namespace rubia::math
{
namespace
{

glm::vec4 matrixRow(const glm::mat4& matrix, glm::length_t row)
{
    return {
        matrix[0][row],
        matrix[1][row],
        matrix[2][row],
        matrix[3][row]
    };
}

Plane normalizedPlane(const glm::vec4& coefficients)
{
    const glm::vec3 normal(coefficients);
    const float length = glm::length(normal);
    if (!std::isfinite(length) || length <= 0.0f)
    {
        throw std::invalid_argument(
            "view projection produced a degenerate frustum plane");
    }
    return {
        normal / length,
        coefficients.w / length
    };
}

} // namespace

Frustum Frustum::fromViewProjection(
    const glm::mat4& viewProjection)
{
    const glm::vec4 row0 = matrixRow(viewProjection, 0);
    const glm::vec4 row1 = matrixRow(viewProjection, 1);
    const glm::vec4 row2 = matrixRow(viewProjection, 2);
    const glm::vec4 row3 = matrixRow(viewProjection, 3);

    Frustum frustum{};
    frustum.planes_ = {
        normalizedPlane(row3 + row0), // Left:   x + w >= 0
        normalizedPlane(row3 - row0), // Right:  w - x >= 0
        normalizedPlane(row3 + row1), // Bottom: y + w >= 0
        normalizedPlane(row3 - row1), // Top:    w - y >= 0
        normalizedPlane(row2),        // Near:   z >= 0 (zero-to-one depth)
        normalizedPlane(row3 - row2)  // Far:    w - z >= 0
    };
    return frustum;
}

bool Frustum::intersects(const Aabb& bounds) const noexcept
{
    if (!bounds.valid())
    {
        return false;
    }

    const glm::vec3 center = bounds.center();
    const glm::vec3 extents = bounds.extents();
    for (const Plane& plane : planes_)
    {
        const float centerDistance =
            glm::dot(plane.normal, center) +
            plane.distance;
        const float projectedRadius = glm::dot(
            glm::abs(plane.normal),
            extents);
        if (centerDistance + projectedRadius < 0.0f)
        {
            return false;
        }
    }
    return true;
}

} // namespace rubia::math
