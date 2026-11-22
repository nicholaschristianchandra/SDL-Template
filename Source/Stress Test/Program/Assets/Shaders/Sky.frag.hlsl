cbuffer Sky : register(b0, space3)
{
    float4 Right;        // w: tan(fov / 2) * aspect
    float4 Up;           // w: tan(fov / 2)
    float4 Forward;
    float4 SunDirection; // w: time
};

float Hash(float3 p)
{
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float4 main(float4 position : SV_Position, float2 screen : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Target0
{
    float3 direction = normalize(Forward.xyz + screen.x * Right.xyz * Right.w + screen.y * Up.xyz * Up.w);
    float3 sun = SunDirection.xyz;
    float day = smoothstep(-0.15, 0.25, sun.y);
    float sunset = saturate(1.0 - abs(sun.y) * 4.0);
    float height = saturate(direction.y);

    float3 zenith = lerp(float3(0.004, 0.008, 0.03), float3(0.15, 0.35, 0.8), day);
    float3 horizon = lerp(float3(0.02, 0.03, 0.07), float3(0.62, 0.75, 0.9), day);
    float towardSun = saturate(dot(normalize(float3(direction.x, 0, direction.z)), normalize(float3(sun.x, 0, sun.z))));
    horizon += float3(0.55, 0.22, 0.06) * sunset * (0.4 + 0.6 * towardSun);
    float3 color = lerp(horizon, zenith, pow(height, 0.5));

    // Sun disk and glow, moon, stars twinkling at night
    float facing = saturate(dot(direction, sun));
    color += float3(1.0, 0.9, 0.7) * (pow(facing, 3000.0) * 40.0 + pow(facing, 12.0) * 0.4 * (day + sunset));
    color += float3(0.8, 0.85, 1.0) * smoothstep(0.9993, 0.9996, dot(direction, -sun)) * 2.0 * (1.0 - day);

    float3 cell = floor(direction * 220.0);
    float star = step(0.996, Hash(cell)) * (0.6 + 0.4 * sin(SunDirection.w * 3.0 + Hash(cell + 7.0) * 40.0));
    color += star * (1.0 - day) * smoothstep(0.0, 0.15, direction.y) * 1.5;

    color = lerp(color, horizon * 0.35, saturate(-direction.y * 4.0));
    return float4(color, 1.0);
}
