//***************************************************************************************
// color.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// Transforms and colors geometry.
//***************************************************************************************

// 光源数量
#ifndef NUM_DIR_LIGHTS
	#define NUM_DIR_LIGHTS 1
#endif 

#ifndef NUM_POINT_LIGHTS
	#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
	#define NUM_SPOT_LIGHTS 0
#endif

#include "LightingUtils.hlsl"

// object constant.
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld; 
};

// material constant.
cbuffer cbMaterial : register(b1)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};

// pass constant.
cbuffer cbPass : register(b2)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3   gEyePosw;
    float    cbPad1;
    float2   gRenderTargetSize;
    float2   gInvRenderTargetSize;
    float    gNearZ;
    float    gFarZ;
    float    gTotalTime;
    float    gDeltaTime;
    
    // 环境光
    float4 gAmbientLight;
    // 光源
    Light gLights[MaxLights];
}

struct VertexIn
{
    float3 PosL  : POSITION;
    float3 NormalL : NORMAL;
};

struct VertexOut
{
    float4 PosH  : SV_POSITION;
    float3 PosW  : POSITION;
    float3 NormalW : NORMAL;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
	float4 PosW = mul(float4(vin.PosL,1.0f),gWorld);
    // Transform to homogeneous clip space.
    vout.PosH = mul(PosW, gViewProj);
    // Just pass vertex color into the pixel shader.
    vout.PosW = PosW;
    // 假设这里是等比缩放，这样的化变化矩阵就是世界矩阵本身，否则要用逆转矩阵.
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    // 顶点法线插值后可能非规范化
    pin.NormalW = normalize(pin.NormalW);
    
    // toEye 
    float3 toEye = normalize(gEyePosw - pin.PosW);
    
    // 间接光
    float4 ambient = gAmbientLight * gDiffuseAlbedo;
    
    // 直接光
    const float shininess = 1.0f - gRoughness;
    Material mat = { gDiffuseAlbedo, gFresnelR0, shininess };
    
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat,pin.PosW, pin.NormalW, toEye,shadowFactor);
    float4 litColor = ambient + directLight;
    
    // 随时间变化的因子.
    float timeFactor = 0.5 * sin(2*3.14*gTotalTime) + 0.5f;
    litColor *= timeFactor;
    litColor.a = gDiffuseAlbedo.a;
    return litColor;
}


