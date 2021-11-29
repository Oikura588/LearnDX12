#define MaxLights 16

struct Light
{
    float3 Strength;
    float FalloffStart; // point/spot light only.
    float3 Direction;   // directional/spot light only.
    float FalloffEnd;   // point/spot light only.
    float3 Position;    // point light only.
    float SpotPower;    // spot light only.
};

struct Material
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Shininess;
};

float CalcAttenuation(float d,float falloffStart,float falloffEnd)
{
    // Linear falloff
    return saturate((falloffEnd-d)/(falloffEnd-falloffStart));
}

// Schlick gives an approximation to Fresnel reflectance (see pg. 233 "Real-Time Rendering 3rd Ed.").
// R0 = ( (n-1)/(n+1) )^2, where n is the index of refraction.

float3 SchlickFresnel(float3 R0,float3 normal,float3 LightVec)
{
    float cosIncidentAngle = saturate(dot(normal,LightVec));
    float f0 = 1-cosIncidentAngle;
    float3 reflectPercent = R0+(1-R0)*(f0*f0*f0*f0);
    return reflectPercent;
}

float3 BlinnPhong(float3 lightStrength,float3 LightVec,float3 normal,float3 toEye,Material mat)
{
    const float m = mat.Shininess*256.0f;
    float3 halfVec = normalize(toEye+LightVec);

    float roughnessFactor = (m+8.0f)*pow(max(dot(halfVec,normal),0.f),m)/8.0f;
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0,halfVec,LightVec);

    float3 specAlbedo = fresnelFactor*roughnessFactor;

    // Our spec formula goes outside [0,1] range
    specAlbedo = specAlbedo/(specAlbedo+1.f);
    return(mat.DiffuseAlbedo.rgb+specAlbedo)*lightStrength;
}

float3 CompmuteDirectionalLight(Light L,Material mat,float3 normal,float3 toEye)
{
    float3 LightVec = -L.Direction;
    float ndotl = max(dot(LightVec,normal),0.F);
    float3 LightStrength = L.Strength*ndotl;
    return BlinnPhong(LightStrength,LightVec,normal,toEye,mat);
}

float3 ComputePointLight(Light L,Material mat,float3 pos,float3 normal,float3 toEye)
{
    // The vector from the surface to the light.
    float3 LightVec = L.Position-pos;

    // Distance.
    float d = length(LightVec);

    // Range test
    if(d>L.FalloffEnd)
        return 0.0f;
    // Normalized the lit vector.
    LightVec /=d;

    float nodtl = max(dot(normal,LightVec),0.0);
    float3 lightStrength = L.Strength*nodtl;

    // Attenuate light by distance.
    float att = CalcAttenuation(d,L.FalloffStart,L.FalloffEnd);
    lightStrength*=att;

    return BlinnPhong(lightStrength,LightVec,normal,toEye,mat);
}

float3 ComputeSpotLight(Light L,Material mat,float3 pos,float3 normal,float3 toEye)
{
    float3 LightVec = L.Position-pos;
    float d = length(LightVec);
    if(d>L.FalloffEnd)
        return 0.0f;
    LightVec/=d;
    float ndotl = max(dot(normal,LightVec),0.0f);
    float3 lightStrength = L.Strength*ndotl;

    float spotFactor= pow(max(dot(-LightVec,L.Direction),0.0f),L.SpotPower);
    lightStrength*=spotFactor;

    return BlinnPhong(lightStrength,LightVec,normal,toEye,mat);
}

float4 ComputeLighting(Light gLights[MaxLights],Material mat,float3 pos,float3 normal,float3 toEye,float3 shadowFactor)
{
    float3 result = 0.0f;
    int i =0;
    
#if (NUM_DI_LIGITS>0)
    for( i = 0;i<NUM_DI_LIGITS;++i)
    {
        result+=shadowFactor[i]*CompmuteDirectionalLight(gLights[i],mat,normal,toEye);    
    }
#endif
    
#if (NUM_POINT_LIGHTS>0)
    for(i = NUM_DI_LIGITS;i<NUM_DI_LIGITS+NUM_POINT_LIGHTS;++i)
    {
        result+=ComputePointLight(gLights[i],mat,pos,normal,toEye);
    }
#endif
    
#if (true||NUM_SPOT_LIGHTS>0)
    for(i=(NUM_DI_LIGITS+NUM_POINT_LIGHTS);i<NUM_DI_LIGITS+NUM_POINT_LIGHTS+NUM_SPOT_LIGHTS;++i)
    {
        result+=ComputeSpotLight(gLights[i],mat,pos,normal,toEye);
    }
#endif
    
    return float4(result,0.0f);
}