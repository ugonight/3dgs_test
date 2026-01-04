struct Vertex
{
    float4 position;
    float4 color;
    float4x4 cov3d;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

cbuffer cb0 : register(b0)
{
    //float4x4 g_mWorldViewProj;
    float4x4 world_transform;
    float4x4 view_transform;
    float4x4 project_transform;
    int2 viewport_size;
    float2 focal;
};

StructuredBuffer<Vertex> Vertices : register(t0);
Texture2D<float> gaussian_texture : register(t1);

SamplerState g_sampler : register(s0);

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
groupshared uint g_count;

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
    
    if (tid == 0)
    {
        uint count, stride;
        Vertices.GetDimensions(count, stride);
        g_count = count;
    }
    GroupMemoryBarrierWithGroupSync();

    bool valid = (vid < g_count);
    
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
    //float4 p_pos = mul(g_mWorldViewProj, float4(v.position.xyz, 1));
    
    //float pointSize = 0.01f; // cov3d活用推奨
    //float2 s = float2(pointSize, pointSize);
    
    uint baseVert = g_baseVert[tid];
    
    //verts[baseVert + 0].position = p_pos + float4(-s.x, -s.y, 0, 0);
    //verts[baseVert + 1].position = p_pos + float4(s.x, -s.y, 0, 0);
    //verts[baseVert + 2].position = p_pos + float4(s.x, s.y, 0, 0);
    //verts[baseVert + 3].position = p_pos + float4(-s.x, s.y, 0, 0);
    
    //verts[baseVert + 0].color = verts[baseVert + 1].color =
    //verts[baseVert + 2].color = verts[baseVert + 3].color = v.color;
    
    float4 world_pos = mul(world_transform, float4(v.position.xyz, 1));
    float4 view_pos = mul(view_transform, world_pos);
    float4 homo_pos = mul(project_transform, view_pos);
    float4 ndc_pos = homo_pos / homo_pos.w;
    
    // proj 3d -> 2d
    float3x3 world_view_trans = (float3x3) mul(view_transform, world_transform);
    float3x3 ray_space_transform = float3x3(
        focal.x / view_pos.z, 0, -focal.x * view_pos.x / (view_pos.z * view_pos.z),
        0, focal.y / view_pos.z, -focal.y * view_pos.y / (view_pos.z * view_pos.z),
        0, 0, 0);
    float3x3 world_view_ray_trans = mul(ray_space_transform, world_view_trans);
    float2x2 cov2d = (float2x2) mul(mul(world_view_ray_trans, (float3x3) v.cov3d), transpose(world_view_ray_trans));
    cov2d[0][0] += 0.3f;
    cov2d[1][1] += 0.3f;
    
    //eigen(vec2d)
    float det = cov2d[0][0] * cov2d[1][1] - cov2d[0][1] * cov2d[1][0];
    float mid = 0.5 * (cov2d[0][0] + cov2d[1][1]);
    float temp = sqrt(max((mid * mid - det), 1e-9f));
    float2 eigen_val = float2(mid - temp, mid + temp);
    float2 eigen_vec_0 = float2(1, 0);
    float2 eigen_vec_1 = float2(0, 1);
    if (cov2d[0][1] != 0)
    {
        float2 eigen_vec_y = (eigen_val - cov2d[0][0]) / cov2d[0][1];
        eigen_vec_0 = normalize(float2(1, eigen_vec_y.x));
        eigen_vec_1 = normalize(float2(1, eigen_vec_y.y));
    }
    //alpha
    float opacity_coefficient = 2 * log(255 * max(1 / 255, v.color.w));
        
    //Ellipse
    float2 axis0 = eigen_vec_0 * sqrt(eigen_val.x * opacity_coefficient) / (float2(viewport_size) * 0.5f);
    float2 axis1 = eigen_vec_1 * sqrt(eigen_val.y * opacity_coefficient) / (float2(viewport_size) * 0.5f);
    if (ndc_pos.x < -1.3f || ndc_pos.x > 1.3f || ndc_pos.y < -1.3f || ndc_pos.y > 1.3f || ndc_pos.z < 0 || ndc_pos.z > 1 || v.color.w < 1 / 255.0f)
    {
        axis0 = float2(1e-5, 1e-5);
        axis1 = axis0;
    }
        
    //2d gaussian mean
    PSInput vertex[4];
    vertex[0].position = homo_pos + float4(axis0 * homo_pos.w, 0, 0) + float4(axis1 * homo_pos.w, 0, 0);
    vertex[0].position.y = -vertex[0].position.y;
    vertex[0].uv = float2(0.f, 0.f);
   
    vertex[1].position = homo_pos - float4(axis0 * homo_pos.w, 0, 0) + float4(axis1 * homo_pos.w, 0, 0);
    vertex[1].position.y = -vertex[1].position.y;
    vertex[1].uv = float2(0.f, 1.f);
    
    vertex[2].position = homo_pos - float4(axis0 * homo_pos.w, 0, 0) - float4(axis1 * homo_pos.w, 0, 0);
    vertex[2].position.y = -vertex[2].position.y;
    vertex[2].uv = float2(1.f, 1.f);
    
    vertex[3].position = homo_pos + float4(axis0 * homo_pos.w, 0, 0) - float4(axis1 * homo_pos.w, 0, 0);
    vertex[3].position.y = -vertex[3].position.y;
    vertex[3].uv = float2(1.f, 0.f);
    
    vertex[0].color = vertex[1].color =
    vertex[2].color = vertex[3].color = v.color;
    
    verts[baseVert + 0] = vertex[0];
    verts[baseVert + 1] = vertex[1];
    verts[baseVert + 2] = vertex[2];
    verts[baseVert + 3] = vertex[3];
    
    uint basePrim = (baseVert / 4) * 2;
    indices[basePrim + 0] = uint3(baseVert + 0, baseVert + 1, baseVert + 2);
    indices[basePrim + 1] = uint3(baseVert + 0, baseVert + 2, baseVert + 3);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    //return input.color;
    
    float alpha = input.color.w * gaussian_texture.Sample(g_sampler, input.uv);
    return float4(input.color.xyz, alpha);
}