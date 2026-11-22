// Linear HDR light to the screen: a soft tone curve, then gamma (the swapchain is plain UNORM)
float3 ToScreen(float3 color)
{
    color = 1.0 - exp(-color * 1.2);
    return pow(saturate(color), 1.0 / 2.2);
}

// What every duck instance holds: two float4 per duck (x, y, z, scale), (angle, spin, phase, unused)
struct Duck
{
    float3 Position;
    float Scale;
    float Angle;
    float Spin;
    float Phase;
};

Duck ReadDuck(float4 a, float4 b)
{
    Duck duck;
    duck.Position = a.xyz;
    duck.Scale = a.w;
    duck.Angle = b.x;
    duck.Spin = b.y;
    duck.Phase = b.z;
    return duck;
}

// The ducks' animation, the same in Animate.comp and in Scene::AnimateDuck on the CPU: they hop, turn and drift
Duck AnimateDuck(Duck duck, float time)
{
    if (duck.Spin > 0.0)
    {
        float orbit = time * 0.03;
        float s = sin(orbit);
        float c = cos(orbit);
        duck.Position = float3(c * duck.Position.x - s * duck.Position.z, abs(sin(time * 5.0 + duck.Phase)) * 0.3 * duck.Scale, s * duck.Position.x + c * duck.Position.z);
        duck.Angle += time * duck.Spin;
    }

    return duck;
}
