// A soft round glow, added to what is behind it
float4 main(float4 position : SV_Position, float2 corner : TEXCOORD0, float3 color : TEXCOORD1) : SV_Target0
{
    float glow = saturate(1.0 - length(corner));
    return float4(color * glow * glow, 0.0);
}
