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
};

cbuffer cb0 : register(b0)
{
    float4x4 g_mWorldViewProj;
};

// HLSL 側 VSMain は float4 position : POSITION を受け取ります。
// C++ 側が R32G32B32_FLOAT（3成分）で渡した場合、ドライバは自動で w 成分を 1.0 にするため、シェーダーには (x,y,z,1.0) が入ります。
PSInput VSMain(float4 position : POSITION, float4 color : COLOR)
{
    PSInput result;

    // 位置変換（クリップ空間）
    result.position = mul(position, g_mWorldViewProj);
    // ベースカラーはそのまま伝える
    result.color = color;

    return result;
}

// ピクセルシェーダで拡散＋アンビエント照明を計算（per-pixel lighting）
float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
