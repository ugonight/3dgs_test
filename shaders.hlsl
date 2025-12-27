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

// HLSL 側 VSMain は float4 position : POSITION を受け取ります。
// C++ 側が R32G32B32_FLOAT（3成分）で渡した場合、ドライバは自動で w 成分を 1.0 にするため、シェーダーには (x,y,z,1.0) が入ります。
PSInput VSMain(float4 position : POSITION, float4 color : COLOR)
{
    PSInput result;

    // SV_POSITION に入れた値はクリップ空間の座標になります。
    // GPU は出力されたクリップ座標を w で除算して正規化デバイス座標（NDC）にし、ビューポート変換でピクセル座標にマップします。
    result.position = position;
    
    result.color = color;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
