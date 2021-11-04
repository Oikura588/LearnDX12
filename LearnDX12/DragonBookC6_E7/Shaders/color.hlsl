cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldVieProj;
    // Begin exercise 16
    // float4 gPulseColor;
    // End exercise 16
    float gTime;
};

struct VertexIn
{
    float3 PosL:POSITION;
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
    vOut.PosH = mul(float4( vIn.PosL,1.0),gWorldVieProj);
    vOut.Color=vIn.Color;
    return vOut;
}

float4 PS(VertexOut pIn):SV_TARGET
{
    float4 finalColor=pIn.Color;
    // Begin Exercise 14
    // finalColor.x += 0.4*sin(gTime);
    // End Exercise 14

    // Begin Exercise 15
    // Clip function will discard current pixel if it's value less than zero.
    // clip(finalColor.x-0.5f);
    // End Exercise 15

    // Begin Exercise 16
    // const float PI = 3.1415926;
    // // 随着时间流逝，得到[0,1]之间的值
    // float s = 0.5*sin(gTime)+0.5f;
    // finalColor = lerp(pIn.Color,gPulseColor,s);
    // End Exercise 16
    
    return finalColor;
}