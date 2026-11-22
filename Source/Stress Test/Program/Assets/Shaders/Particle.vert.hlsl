cbuffer Particles : register(b0, space1)
{
    float4x4 ViewProjection;
    float4 CameraRight;
    float4 CameraUp;
    float4 Params; // x: time, y: brightness
    float4 LightColors[4];
};

struct Output
{
    float4 Position : SV_Position;
    float2 Corner : TEXCOORD0;
    float3 Color : TEXCOORD1;
};

float Hash(uint n, uint salt)
{
    n = n * 747796405u + salt * 2891336453u;
    n = ((n >> ((n >> 28u) + 4u)) ^ n) * 277803737u;
    return float((n >> 22u) ^ n) / 4294967295.0;
}

// Fireflies, made from nothing but their number: each drifts on its own circle, in the colors of the four lights
Output main(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    const float2 corners[6] = { float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1) };
    float2 corner = corners[vertex];
    float time = Params.x;
    float radius = 1.0 + 13.0 * Hash(instance, 1);
    float direction = Hash(instance, 2) > 0.5 ? 1.0 : -1.0;
    float angle = Hash(instance, 3) * 6.2831853 + time * (0.1 + 0.4 * Hash(instance, 4)) * direction;
    float height = 0.2 + 3.5 * Hash(instance, 5) + 0.3 * sin(time * 1.3 + Hash(instance, 6) * 6.2831853);
    float size = 0.03 + 0.05 * Hash(instance, 7);
    float3 world = float3(cos(angle) * radius, height, sin(angle) * radius);
    world += (corner.x * CameraRight.xyz + corner.y * CameraUp.xyz) * size;

    Output output;
    output.Position = mul(ViewProjection, float4(world, 1.0));
    output.Corner = corner;
    output.Color = LightColors[instance % 4].rgb * Params.y * (0.6 + 0.4 * sin(time * 4.0 + Hash(instance, 8) * 40.0));
    return output;
}
