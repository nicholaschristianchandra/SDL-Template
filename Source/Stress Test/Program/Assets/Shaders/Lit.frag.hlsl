Texture2D<float4> Albedo : register(t0, space2);
SamplerState AlbedoSampler : register(s0, space2);
Texture2D<float4> NormalMap : register(t1, space2);
SamplerState NormalSampler : register(s1, space2);
Texture2D<float> ShadowMap : register(t2, space2);
SamplerComparisonState ShadowSampler : register(s2, space2);

cbuffer Lighting : register(b0, space3)
{
    float4x4 LightViewProjection; // the shadow map's
    float4 ShadowParams;          // x: shadows on, y: one texel, z: which light casts them (0 sun, 1 moon)
    float4 CameraPosition;
    float4 SunDirection;
    float4 SunColor;
    float4 MoonDirection;
    float4 MoonColor;
    float4 Ambient;
    float4 Fog;               // rgb: the horizon's color, w: density
    float4 LightPositions[4]; // w: range
    float4 LightColors[4];    // w: intensity
    float4 Material;          // x: normal map on, y: specular strength, z: shininess
};

struct Input
{
    float4 Position : SV_Position;
    float3 World : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV : TEXCOORD2;
    float4 Tangent : TEXCOORD3;
};

float3 Shade(float3 albedo, float3 normal, float3 view, float3 toLight, float3 color)
{
    float3 halfway = normalize(toLight + view);
    float diffuse = saturate(dot(normal, toLight));
    float specular = pow(saturate(dot(normal, halfway)), Material.z) * Material.y * (diffuse > 0.0 ? 1.0 : 0.0);
    return (albedo * diffuse + specular) * color;
}

// 1 lit, 0 in shadow, softened over 3 x 3 texels
float Shadow(float3 world)
{
    if (ShadowParams.x < 0.5)
    {
        return 1.0;
    }

    float4 clip = mul(LightViewProjection, float4(world, 1.0));
    float3 ndc = clip.xyz / clip.w;
    float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);

    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0)
    {
        return 1.0;
    }

    float lit = 0.0;

    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            lit += ShadowMap.SampleCmpLevelZero(ShadowSampler, uv + float2(x, y) * ShadowParams.y, ndc.z);
        }
    }

    return lit / 9.0;
}

float4 main(Input input) : SV_Target0
{
    float3 albedo = Albedo.Sample(AlbedoSampler, input.UV).rgb;
    float3 normal = normalize(input.Normal);

    if (Material.x > 0.5)
    {
        float3 tangent = normalize(input.Tangent.xyz - normal * dot(normal, input.Tangent.xyz));
        float3 bitangent = cross(normal, tangent) * input.Tangent.w;
        float3 bumped = NormalMap.Sample(NormalSampler, input.UV).xyz * 2.0 - 1.0;
        normal = normalize(tangent * bumped.x + bitangent * bumped.y + normal * bumped.z);
    }

    float3 toCamera = CameraPosition.xyz - input.World;
    float3 view = normalize(toCamera);
    float shadow = Shadow(input.World);
    float3 color = albedo * Ambient.rgb * (0.6 + 0.4 * normal.y);
    color += Shade(albedo, normal, view, SunDirection.xyz, SunColor.rgb) * (ShadowParams.z < 0.5 ? shadow : 1.0);
    color += Shade(albedo, normal, view, MoonDirection.xyz, MoonColor.rgb) * (ShadowParams.z > 0.5 ? shadow : 1.0);

    [unroll]
    for (int i = 0; i < 4; i++)
    {
        float3 toLight = LightPositions[i].xyz - input.World;
        float distance = length(toLight);
        float window = saturate(1.0 - pow(distance / LightPositions[i].w, 4.0));
        float attenuation = window * window / (1.0 + distance * distance);
        color += Shade(albedo, normal, view, toLight / distance, LightColors[i].rgb * LightColors[i].w * attenuation);
    }

    float fog = 1.0 - exp(-length(toCamera) * Fog.w);
    return float4(lerp(color, Fog.rgb, fog), 1.0);
}
