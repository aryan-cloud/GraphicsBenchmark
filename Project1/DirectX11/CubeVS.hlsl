#include "CubeShaderIO.hlsli"

cbuffer CubeTransforms : register(b0)
{
    row_major float4x4 worldViewProjection;
};

VertexOutput main(float3 position : POSITION, float3 color : COLOR)
{
    VertexOutput output;
    output.position = mul(float4(position, 1.0f), worldViewProjection);
    output.color = color;
    return output;
}
