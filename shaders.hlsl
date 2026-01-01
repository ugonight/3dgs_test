//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

struct Vertex
{
    float3 position;
    float4 color;
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

//// HLSL 側 VSMain は float4 position : POSITION を受け取ります。
//// C++ 側が R32G32B32_FLOAT（3成分）で渡した場合、ドライバは自動で w 成分を 1.0 にするため、シェーダーには (x,y,z,1.0) が入ります。
//PSInput VSMain(float4 position : POSITION, float4 color : COLOR)
//{
//    PSInput result;

//    // 位置変換（クリップ空間）
//    result.position = mul(g_mWorldViewProj, position);
//    // ベースカラーはそのまま伝える
//    result.color = color;

//    return result;
//}

[NumThreads(128, 1, 1)]
[OutputTopology("triangle")]
void MSMain(
    uint gtid : SV_GroupThreadID, // グループ内のスレッド ID
    uint gid : SV_GroupID, // グループ ID
    out vertices PSInput verts[128],
    out indices uint3 tris[64]
)
{
    SetMeshOutputCounts(128, 64);
    
    uint index = gid * 128 + gtid; // 各スレッドが担当する頂点のインデックス
    
    Vertex v = Vertices[index];
    
    // 点を中心に小さなクワッドを作成（サイズ: 0.01単位、カメラ向きにするためビルボード化可能）
    float size = 0.01f;
    float3 offsets[4] =
    {
        float3(-size, -size, 0), // 左下
        float3(size, -size, 0), // 右下
        float3(-size, size, 0), // 左上
        float3(size, size, 0) // 右上
    };
    
    // 頂点出力（クワッドの4頂点）
    uint vertBase = gtid * 4; // スレッドごとの出力オフセット
    for (uint i = 0; i < 4; ++i)
    {
        float3 worldPos = v.position + offsets[i]; // オフセット追加（ビルボード化でカメラ方向調整）
        verts[vertBase + i].position = mul(g_mWorldViewProj, float4(worldPos, 1.0f));
        verts[vertBase + i].color = v.color;
    }

    // インデックス出力（2三角形）
    uint primBase = gtid * 2;
    tris[primBase + 0] = uint3(vertBase + 0, vertBase + 1, vertBase + 2); // 第一三角形
    tris[primBase + 1] = uint3(vertBase + 1, vertBase + 2, vertBase + 3); // 第二三角形
}

// ピクセルシェーダで拡散＋アンビエント照明を計算（per-pixel lighting）
float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
