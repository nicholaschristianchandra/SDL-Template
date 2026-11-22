Texture2D<float4> Source : register(t0, space2);
SamplerState SourceSampler : register(s0, space2);

cbuffer Blur : register(b0, space3)
{
    float4 Params; // xy: one texel, along the direction of this pass
};

// One direction of a separable Gaussian blur
float4 main(float4 position : SV_Position, float2 screen : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Target0
{
    const float weights[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };
    float3 color = Source.Sample(SourceSampler, uv).rgb * weights[0];

    [unroll]
    for (int i = 1; i < 5; i++)
    {
        float2 offset = Params.xy * (i * 1.5);
        color += Source.Sample(SourceSampler, uv + offset).rgb * weights[i];
        color += Source.Sample(SourceSampler, uv - offset).rgb * weights[i];
    }

    return float4(color, 1.0);
}
