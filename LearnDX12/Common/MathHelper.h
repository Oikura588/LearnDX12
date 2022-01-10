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
    
    static const float Pi;
    static const float Infinity;
};