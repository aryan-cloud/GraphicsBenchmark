#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <vector>

struct CubeScene
{
    // The cubes share orientation and scale, so each world transform is a translation.
    std::vector<DirectX::XMFLOAT3> positions;
    float boundingRadius = 0.0f;
};

CubeScene CreateCubeScene(std::uint32_t count);
DirectX::XMMATRIX CubeViewProjection(const CubeScene& scene, float aspect);
