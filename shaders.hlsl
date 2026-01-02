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


#define TASK_THREADS_PER_GROUP 32  // タスクグループサイズ（調整）
#define POINTS_PER_TASK 2048       // 1タスクあたりの点数（THREADS_PER_GROUP*倍数）

#define MAX_VERTS 256
#define MAX_PRIMS 128
#define THREADS_PER_GROUP 64

struct Payload
{
    uint meshGroupCount;
    uint startOffset;
};

groupshared Payload g_payload;

[numthreads(TASK_THREADS_PER_GROUP, 1, 1)]
void ASMain(uint tid : SV_GroupThreadID, uint gid : SV_GroupID)
{
    if (tid == 0)
    {
        uint totalPoints = 0;
        uint stride = 0;
        Vertices.GetDimensions(totalPoints, stride); // 点数取得

        uint startPoint = gid * POINTS_PER_TASK;
        uint endPoint = min(startPoint + POINTS_PER_TASK, totalPoints);
        uint pointCount = endPoint - startPoint;

        // メッシュグループ数計算（例: pointCount / THREADS_PER_GROUP）
        g_payload.meshGroupCount = (pointCount + THREADS_PER_GROUP - 1) / THREADS_PER_GROUP;
        g_payload.startOffset = startPoint;
        
        // 視野カリングなど追加可能
    }
    GroupMemoryBarrierWithGroupSync();

    // メッシュグループをDispatch（0個以上可能）
    DispatchMesh(g_payload.meshGroupCount, 1, 1, g_payload);
}

groupshared uint g_vertCount;
groupshared uint g_primCount;
groupshared uint g_baseVert[THREADS_PER_GROUP];

[NumThreads(THREADS_PER_GROUP, 1, 1)]
[OutputTopology("triangle")]
void MSMain(
    uint tid : SV_GroupIndex,
    uint gid : SV_GroupID,
    in payload Payload p,
    out vertices PSInput verts[MAX_VERTS],
    out indices uint3 indices[MAX_PRIMS]
)
{
    if (tid == 0)
    {
        g_vertCount = 0;
        g_primCount = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint vid = p.startOffset + (gid * THREADS_PER_GROUP + tid);
    
    uint count, stride;
    if (tid == 0)
    {
        Vertices.GetDimensions(count, stride);
    }
    GroupMemoryBarrierWithGroupSync();

    bool valid = (vid < count);
    
    uint localVertOffset = 0;
    if (valid)
    {
        uint originalVert;
        InterlockedAdd(g_vertCount, 4, originalVert);
        localVertOffset = originalVert;
        
        uint originalPrim;
        InterlockedAdd(g_primCount, 2, originalPrim);
    }
    GroupMemoryBarrierWithGroupSync();
    
    g_baseVert[tid] = localVertOffset;
    GroupMemoryBarrierWithGroupSync();
    
    SetMeshOutputCounts(g_vertCount, g_primCount);
    
    if (!valid)
        return;

    Vertex v = Vertices[vid];
    float4 p_pos = mul(g_mWorldViewProj, float4(v.position.xyz, 1));
    
    float pointSize = 0.01f; // cov3d活用推奨
    float2 s = float2(pointSize, pointSize);
    
    uint baseVert = g_baseVert[tid];
    
    verts[baseVert + 0].position = p_pos + float4(-s.x, -s.y, 0, 0);
    verts[baseVert + 1].position = p_pos + float4(s.x, -s.y, 0, 0);
    verts[baseVert + 2].position = p_pos + float4(s.x, s.y, 0, 0);
    verts[baseVert + 3].position = p_pos + float4(-s.x, s.y, 0, 0);
    
    verts[baseVert + 0].color = verts[baseVert + 1].color =
    verts[baseVert + 2].color = verts[baseVert + 3].color = v.color;
    
    uint basePrim = (baseVert / 4) * 2;
    indices[basePrim + 0] = uint3(baseVert + 0, baseVert + 1, baseVert + 2);
    indices[basePrim + 1] = uint3(baseVert + 0, baseVert + 2, baseVert + 3);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}