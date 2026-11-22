#include "CubeShaderIO.hlsli"

VertexOutput main(float3 position : POSITION, float3 color : COLOR,
    float4 row0 : INSTANCE_TRANSFORM0, float4 row1 : INSTANCE_TRANSFORM1,
    float4 row2 : INSTANCE_TRANSFORM2, float4 row3 : INSTANCE_TRANSFORM3)
{
    const row_major float4x4 worldViewProjection = float4x4(row0, row1, row2, row3);
    VertexOutput output;
    output.position = mul(float4(position, 1.0f), worldViewProjection);
    output.color = color;
    return output;
}
