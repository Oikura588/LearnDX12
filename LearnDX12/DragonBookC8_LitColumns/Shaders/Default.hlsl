#ifndef NUM_DIR_LIGHTS
    #define NUM_DI_LIGITS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

#include "LightingUtil.hlsl"

cbuffer cbPerObject:register(b0)
{
    float4x4 gWorld;
};

cbuffer cbMaterial:register(b1)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};


cbuffer cbPass:register(b2)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;

    Light gLights[MaxLights];
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL:NORMAL;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW: NORMAL;
};

VertexOut VS(VertexIn vIn)
{
    VertexOut vOut = (VertexOut)0.f;

    float4 PosW = mul(float4(vIn.PosL,1.0),gWorld);
    vOut.PosW = PosW.xyz;
    // Assums nouniform scaling; otherwise need to use inverse-transpose of world matrix.
    vOut.NormalW = mul(vIn.NormalL,(float3x3)gWorld);
    vOut.PosH = mul(PosW,gViewProj);
    return vOut;
}

float4 PS(VertexOut pIn): SV_TARGET
{
    // Interpolating normal can unnormalize it, so renormalize it.
    pIn.NormalW = normalize(pIn.NormalW);

    float3 toEyeW = normalize(gEyePosW-pIn.PosW);

    // 简介光
    float4 ambient = gAmbientLight*gDiffuseAlbedo;

    const float shininess = 1.0f-gRoughness;
    Material mat = {gDiffuseAlbedo,gFresnelR0,shininess};
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights,mat,pIn.PosW,pIn.NormalW,toEyeW,shadowFactor);
    float4 litColor = ambient+directLight;

    // common convention to take alpha from diffuse mat.
    litColor.a = gDiffuseAlbedo.a;
    return litColor;

    
    
}