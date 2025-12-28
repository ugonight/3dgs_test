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

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float3 normal : NORMAL;
};

cbuffer cb0 : register(b0)
{
    float4x4 g_mWorldViewProj;
    float4x4 g_invModel;
    float3 g_lightDir;
    float g_ambient;
};

// HLSL 側 VSMain は float4 position : POSITION を受け取ります。
// C++ 側が R32G32B32_FLOAT（3成分）で渡した場合、ドライバは自動で w 成分を 1.0 にするため、シェーダーには (x,y,z,1.0) が入ります。
PSInput VSMain(float4 position : POSITION, float4 color : COLOR, float3 normal : NORMAL)
{
    PSInput result;

    // 位置変換（クリップ空間）
    result.position = mul(position, g_mWorldViewProj);

    // 法線を適切に変換（inverse-transpose を用いる）。w=0 で方向ベクトルとして扱う。
    result.normal = normalize(mul(float4(normal, 0.0f), g_invModel).xyz);

    // ベースカラーはそのまま伝える
    result.color = color;

    return result;
}

// ピクセルシェーダで拡散＋アンビエント照明を計算（per-pixel lighting）
float4 PSMain(PSInput input) : SV_TARGET
{
    // 光方向を同じ空間に変換して正規化（w=0: 平行光）
    float3 transformedLight = normalize(mul(float4(g_lightDir, 0.0f), g_invModel).xyz);

    // 法線は VS で正規化済みだが念のため正規化
    float3 n = normalize(input.normal);

    // Lambertian 拡散
    float diffuse = saturate(dot(n, transformedLight));

    // アンビエント + 拡散
    float3 color = input.color.rgb * (g_ambient + diffuse);

    return float4(color, input.color.a);
}
