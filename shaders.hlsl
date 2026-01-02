struct Vertex
{
    float4 position;
    float4 color;
    float3x3 cov3d;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

cbuffer cb0 : register(b0)
{
    float4x4 g_mWorldViewProj;
};

StructuredBuffer<Vertex> Vertices : register(t0);

#define THREADS_PER_GROUP 128

[NumThreads(THREADS_PER_GROUP, 1, 1)]
[OutputTopology("triangle")]
void MSMain(
    uint tid : SV_GroupIndex,
    uint gid : SV_GroupID,
    out vertices PSInput verts[4],
    out indices uint3 indices[2]
)
{
    uint vid = gid * THREADS_PER_GROUP + tid;
    
    uint count, stride;
    Vertices.GetDimensions(count, stride);

    bool valid = (vid < count);

    SetMeshOutputCounts(valid ? 4 : 0, valid ? 2 : 0);

    if (!valid)
        return;

    Vertex v = Vertices[vid];
    float4 p = mul(g_mWorldViewProj, float4(v.position.xyz, 1));

    float pointSize = 0.01f;
    float2 s = float2(pointSize, pointSize);

    verts[0].position = p + float4(-s.x, s.y, 0, 0);
    verts[1].position = p + float4(-s.x, -s.y, 0, 0);
    verts[2].position = p + float4(s.x, -s.y, 0, 0);
    verts[3].position = p + float4(s.x, s.y, 0, 0);

    verts[0].color =
    verts[1].color =
    verts[2].color = 
    verts[3].color = v.color;

    indices[0] = uint3(0, 1, 3);
    indices[1] = uint3(2, 3, 1);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}