#include "Common.hlsli"

cbuffer Transform : register(b0, space1)
{
    float4x4 ViewProjection;
    float4 Params; // x: 1 for ducks, y: time, z: first duck (one draw per duck), w: 1 when the ducks aren't animated yet
};

ByteAddressBuffer Instances : register(t0, space0);

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
    float3 World : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV : TEXCOORD2;
    float4 Tangent : TEXCOORD3;
};

float3 RotateY(float3 v, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

Output main(Input input, uint instance : SV_InstanceID)
{
    float3 world = input.Position;
    float3 normal = input.Normal;

    if (Params.x > 0.5)
    {
        uint index = instance + (uint)Params.z;
        uint address = index * 32;
        Duck duck = ReadDuck(asfloat(Instances.Load4(address)), asfloat(Instances.Load4(address + 16)));
        float angle = duck.Angle + Params.w * Params.y * duck.Spin;
        world = RotateY(world * duck.Scale, angle) + duck.Position;
        normal = RotateY(normal, angle);
    }

    Output output;
    output.Position = mul(ViewProjection, float4(world, 1.0));
    output.World = world;
    output.Normal = normal;
    output.UV = input.UV;
    output.Tangent = input.Tangent;
    return output;
}
