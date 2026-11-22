#include "CubeShaderIO.hlsli"

float4 main(VertexOutput input) : SV_TARGET
{
    return float4(input.color, 1.0f);
}
