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
    
  // 光源
    Light gLights[MaxLights];
}

struct VertexIn
{
  float3 PosL  : POSITION;
  float3 Normal : NORMAL;
  float4 Color : COLOR;
};

struct VertexOut
{
  float4 PosH  : SV_POSITION;
  float4 Color : COLOR;
};

VertexOut VS(VertexIn vin)
{
  VertexOut vout;
	float4 PosW = mul(float4(vin.PosL,1.0f),gWorld);
  // Transform to homogeneous clip space.
  vout.PosH = mul(PosW, gViewProj);
	
  // Just pass vertex color into the pixel shader.
  vout.Color = vin.Color;
    
  return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
  return pin.Color;
}


