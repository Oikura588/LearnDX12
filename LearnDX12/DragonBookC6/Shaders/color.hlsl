//***************************************************************************************
// color.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// Transforms and colors geometry.
//***************************************************************************************
// 光源数量
#ifndef NUM_DIR_LIGHTS
	#define NUM_DIR_LIGHTS 3
#endif 

#ifndef NUM_POINT_LIGHTS
	#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
	#define NUM_SPOT_LIGHTS 0
#endif

#include "LightingUtils.hlsl"

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld; 
    float4x4 gInvWorld;
};
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
    
    // 环境光和光源
    float4 gAmbientLight;
    Light gLights[MaxLights];
}

struct VertexIn
{
  float3 PosL  : POSITION;
  float4 Color : COLOR;
  float3 NormalL: NORMAL;
};

struct VertexOut
{
    float4 PosH  : SV_POSITION;
    float3 PosW : POSITION;
    float4 Color : COLOR;
    float3 NormalW:NORMAL;
};

VertexOut VS(VertexIn vin)
{
  VertexOut vout;
	float4 PosW = mul(float4(vin.PosL,1.0f),gWorld);
  // Transform to homogeneous clip space.
    vout.PosH = mul(PosW, gViewProj);
    vout.PosW = PosW;
    vout.NormalW = mul(vin.NormalL, transpose((float3x3) gInvWorld));
  // Just pass vertex color into the pixel shader.
  vout.Color = vin.Color;
    
  return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    pin.NormalW = normalize(pin.NormalW);
    
    // toEye
    float3 toEye = normalize(gEyePosw - pin.PosW);
    
    // 间接光
    float4 ambient = gAmbientLight * gDiffuseAlbedo;
    
    // 直接光
    const float shiness = 1.0f - gRoughness;
    float3 shadowFactor = 1.0f;
    Material mat = { gDiffuseAlbedo, gFresnelR0, shiness };
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW, pin.NormalW, toEye, shadowFactor);
    
    float4 litColor = (1.0*(ambient + directLight) + 0.0*pin.Color);
    litColor.a = gDiffuseAlbedo.a;
    return litColor;
}


