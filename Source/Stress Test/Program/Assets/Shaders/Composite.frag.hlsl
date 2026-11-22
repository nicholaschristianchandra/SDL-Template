#include "Common.hlsli"

Texture2D<float4> Scene : register(t0, space2);
SamplerState SceneSampler : register(s0, space2);
Texture2D<float4> Bloom : register(t1, space2);
SamplerState BloomSampler : register(s1, space2);

cbuffer Composite : register(b0, space3)
{
    float4 Params; // x: bloom strength
};

// The HDR scene plus its glow, tone mapped to the screen
float4 main(float4 position : SV_Position, float2 screen : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Target0
{
    float3 color = Scene.Sample(SceneSampler, uv).rgb + Bloom.Sample(BloomSampler, uv).rgb * Params.x;
    return float4(ToScreen(color), 1.0);
}
