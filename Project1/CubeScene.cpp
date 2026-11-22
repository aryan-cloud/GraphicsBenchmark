#include "CubeScene.h"

#include <algorithm>
#include <cassert>
#include <cmath>

CubeScene CreateCubeScene(std::uint32_t count)
{
    assert(count == 1 || count == 100 || count == 1000 || count == 10000);
    constexpr float spacing = 3.0f;
    const std::uint32_t columns = count == 1000 ? 40 : static_cast<std::uint32_t>(std::sqrt(count));
    const std::uint32_t rows = count / columns;
    const float halfX = (columns - 1) * spacing * 0.5f;
    const float halfZ = (rows - 1) * spacing * 0.5f;

    CubeScene scene;
    scene.positions.reserve(count);
    for (std::uint32_t row = 0; row < rows; ++row)
    {
        for (std::uint32_t column = 0; column < columns; ++column)
            scene.positions.emplace_back(column * spacing - halfX, 0.0f, row * spacing - halfZ);
    }

    // Include the outer corners of the cubes, which extend one unit from their centers.
    scene.boundingRadius = std::sqrt((halfX + 1.0f) * (halfX + 1.0f) +
        1.0f + (halfZ + 1.0f) * (halfZ + 1.0f));
    return scene;
}

DirectX::XMMATRIX CubeViewProjection(const CubeScene& scene, float aspect)
{
    using namespace DirectX;

    constexpr float verticalFov = XM_PIDIV4;
    const float limitingHalfFov = std::atan(std::tan(verticalFov * 0.5f) * (std::min)(aspect, 1.0f));
    const float paddedRadius = scene.boundingRadius * 1.1f;
    const float fitDistance = paddedRadius / std::sin(limitingHalfFov);

    const XMVECTOR originalEye = XMVectorSet(4.0f, 3.0f, -6.0f, 0.0f);
    const float originalDistance = XMVectorGetX(XMVector3Length(originalEye));
    const bool singleCube = scene.positions.size() == 1;
    const float distance = (std::max)(originalDistance, fitDistance);
    const XMVECTOR eye = distance == originalDistance ? originalEye :
        XMVectorScale(originalEye, distance / originalDistance);
    const XMMATRIX view = XMMatrixLookAtLH(eye, XMVectorZero(), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    const float nearZ = singleCube ? 0.1f : distance - paddedRadius;
    const float farZ = singleCube ? (std::max)(100.0f, distance + paddedRadius) : distance + paddedRadius;
    return view * XMMatrixPerspectiveFovLH(verticalFov, aspect, nearZ, farZ);
}
