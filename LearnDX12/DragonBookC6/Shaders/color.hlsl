cbuffer cbPerObject:register(b0)
{
  float4x4 gWorldViewProj;  
};

struct VertexIn
{
  float3 pos:POSITION;
  float4 Color:COLOR;
};

struct VertexOut
{
  float4 PosH:SV_POSITION;
  float4 Color:COLOR;
};

VertexOut VS(VertexIn vIn)
{
  VertexOut vOut;
  vOut.PosH = mul(float4(vIn.pos,1.0),gWorldViewProj);
  vOut.Color = vIn.Color;
  return  vOut;
}

float4 PS(VertexOut pIn) :SV_Target
{
  return pIn.Color;
}

