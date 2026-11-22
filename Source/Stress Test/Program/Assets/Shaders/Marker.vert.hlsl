cbuffer Lights : register(b0, space1)
{
    float4x4 ViewProjection;
    float4 LightPositions[4];
    float4 LightColors[4];
};

struct Input
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV : TEXCOORD2;
    float4 Tangent : TEXCOORD3;
};

struct Output
{
    float4 Position : SV_Position;
    float3 Color : TEXCOORD0;
};

// A small glowing sphere at each light
Output main(Input input, uint instance : SV_InstanceID)
{
    Output output;
    float3 world = LightPositions[instance].xyz + input.Position * 0.09;
    output.Position = mul(ViewProjection, float4(world, 1.0));
    output.Color = LightColors[instance].rgb * (3.0 + 3.0 * saturate(input.Normal.y));
    return output;
}
