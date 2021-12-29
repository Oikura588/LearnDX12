#pragma once
#include <cstdint>
#include <DirectXMath.h>

class GeometryGenerator
{
public:
    using uint16 = std::uint16_t;
    using uint32 = std::uint32_t;
    // 顶点信息，坐标、法线、切线、纹理坐标.
    struct Vertex{
        Vertex(){}
        Vertex(
            const DirectX::XMFLOAT3& p,
            const DirectX::XMFLOAT3& n,
            const DirectX::XMFLOAT3& t,
            const DirectX::XMFLOAT2& uv
            ):Position(p),Normal(n),TangentU(t),TexC(uv)
            {}
        Vertex(
            float px,float py,float pz,
            float nx,float ny,float nz,
            float tx,float ty,float tz,
            float u ,float v
        ):Position(px,py,pz),Normal(nx,ny,nz),TangentU(tx,ty,tz),TexC(u,v){}
        
        DirectX::XMFLOAT3 Position;
        DirectX::XMFLOAT3 Normal;
        DirectX::XMFLOAT3 TangentU;
        DirectX::XMFLOAT2 TexC;
    };
    
    
};
