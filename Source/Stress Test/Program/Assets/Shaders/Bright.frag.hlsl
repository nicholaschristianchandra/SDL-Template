Texture2D<float4> Scene : register(t0, space2);
SamplerState SceneSampler : register(s0, space2);

cbuffer Bright : register(b0, space3)
{
    float4 Params; // x: threshold
};

// What glows: the light above the threshold, at half size
float4 main(float4 position : SV_Position, float2 screen : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Target0
{
    float3 color = Scene.Sample(SceneSampler, uv).rgb;
    float brightness = max(color.r, max(color.g, color.b));
    float keep = saturate((brightness - Params.x) / max(brightness, 0.0001));
    return float4(color * keep, 1.0);
}
