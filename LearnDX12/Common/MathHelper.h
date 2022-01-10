#pragma once
#include <Windows.h>
#include <DirectXMath.h>
#include <cstdint>

class MathHelper
{
public:

    template<typename T>
    static float Clamp(const T&x,const T& low,const T& high)
    {
        return x<low?low:(x>high?high:x);
    }

    static int Rand(int a, int b)
    {
        return a + rand()%(b-a+1);
    }

    static float RandF()
    {
        return float(rand())/(float)RAND_MAX;
    }
    static float RandF(float a, float b)
    {
        return a + RandF()*(b-a);
    }

    static DirectX::XMFLOAT4X4 Identity4x4()
    {
        static DirectX::XMFLOAT4X4 I(
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        );
        return I;
    }

    static DirectX::XMVECTOR ComputeNormal(DirectX::FXMVECTOR p0, DirectX::FXMVECTOR p1, DirectX::FXMVECTOR p2)
    {
        using namespace DirectX;
        DirectX::XMVECTOR u = p1 - p0;
        DirectX::XMVECTOR v = p2 - p0;

        return XMVector3Normalize(XMVector3Cross(u,v));
    }

    static DirectX::XMMATRIX InverseTranspose(DirectX::CXMMATRIX M)
    {
		using namespace DirectX;
        DirectX::XMMATRIX A = M ;
        // 平移项清零。因为只有点类才需要平移变换，此处的逆转矩阵只是为了变化法向量
        A.r[3] = XMVectorSet(0.F,0.F,0.F,1.F);
        DirectX::XMVECTOR det = XMMatrixDeterminant(A);
        return XMMatrixTranspose(XMMatrixInverse(&det,A));
    }
    static const float Pi;
    static const float Infinity;
};