// One triangle covering the screen, for the sky and the post-processing passes
struct Output
{
    float4 Position : SV_Position;
    float2 Screen : TEXCOORD0; // -1 to 1, y up
    float2 UV : TEXCOORD1;     // 0 to 1, y down
};

Output main(uint id : SV_VertexID)
{
    float2 corner = float2((id << 1) & 2, id & 2);
    Output output;
    output.Screen = corner * 2.0 - 1.0;
    output.UV = float2(corner.x, 1.0 - corner.y);
    output.Position = float4(output.Screen, 1.0, 1.0);
    return output;
}
