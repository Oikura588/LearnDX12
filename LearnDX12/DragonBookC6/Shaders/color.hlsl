//***************************************************************************************
// color.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// Transforms and colors geometry.
//***************************************************************************************

cbuffer cbPerObject : register(b0)
{
  float4x4 gWorldViewProj;
  float gTime;
};

struct VertexIn
{
  float3 PosL  : POSITION;
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
	
  // Transform to homogeneous clip space.
  vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);
	
  // Just pass vertex color into the pixel shader.
  vin.Color.x += 0.5*sin(1*gTime);
  vin.Color.y += 0.5*sin(2*gTime);
  vin.Color.z += 0.5*sin(3*gTime);
  vout.Color = vin.Color;
    
  return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
  return pin.Color;
}


