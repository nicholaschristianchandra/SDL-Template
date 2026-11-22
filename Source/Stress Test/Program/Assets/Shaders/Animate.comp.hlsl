#include "Common.hlsli"

ByteAddressBuffer Instances : register(t0, space0);
RWByteAddressBuffer Animated : register(u0, space1);

cbuffer Animation : register(b0, space2)
{
    float4 Params; // x: time, y: duck count
};

// Every duck's place this frame, from where it started
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint index = id.x;

    if (index >= (uint)Params.y)
    {
        return;
    }

    uint address = index * 32;
    Duck duck = AnimateDuck(ReadDuck(asfloat(Instances.Load4(address)), asfloat(Instances.Load4(address + 16))), Params.x);
    Animated.Store4(address, asuint(float4(duck.Position, duck.Scale)));
    Animated.Store4(address + 16, asuint(float4(duck.Angle, duck.Spin, duck.Phase, 0.0)));
}
