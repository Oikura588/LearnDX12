#include "Waves.h"
#include <ppl.h>

using namespace DirectX;

Waves::Waves(int m, int n, float dx, float dt, float speed, float damping)
{
    mNumRows = m;
    mNumColumns = n;

    mVertexCount = m*n;
    mTriangleCount = (m-1)*(n-1)*2;

    mTimeStep = dt;
    mSpatialStep = dx;

    float d = damping*dt+2.0f;
    float e = (speed*speed)*(dt*dt)/(dx*dx);
    mK1 = (damping*dt - 2.0f)/d;
    mK2 = (4.0f-8.0f*e)/d;
    mK3 = (2.0f*e)/d;

    mPrevSolution.resize(m*n);
    mCurrSolution.resize(mVertexCount);
    mNormals.resize(mVertexCount);
    mTangentX.resize(mVertexCount);

    // Gen gird vertices.
    float halfWidth = (n-1)*dx*0.5f;
    float halfDepth = (m-1)*dx*0.5f;
    for(int i =0;i<m;++i)
    {
        float z = halfDepth - i*dx;
        for(int j = 0;j<n;++j)
        {
            float x = -halfWidth+j*dx;
            mPrevSolution[i*n+j] = DirectX::XMFLOAT3(x,0.f,z);
            mCurrSolution[i*n+j] = DirectX::XMFLOAT3(x,0.f,z);
            mNormals[i*n+j] =DirectX::XMFLOAT3(0.F,1.F,0.F);
            mTangentX[i*n+j] = DirectX::XMFLOAT3(1.F,0.F,0.F);
        }
    }
}

Waves::~Waves()
{
}

int Waves::RowCount() const
{
    return mNumColumns;
}

int Waves::ColumnCount() const
{
    return mNumColumns;
}

int Waves::VertexCount() const
{
    return mVertexCount;
}

int Waves::TriangleCount() const
{
    return mTriangleCount;
}

float Waves::Width() const
{
    return mNumColumns*mSpatialStep;
}

float Waves::Height() const
{
    return mNumRows*mSpatialStep;
}

void Waves::Update(float dt)
{
    static float t = 0.f;
    t+=dt;

    if(t>=mTimeStep)
    {
        concurrency::parallel_for(1,mNumRows-1,[this](int i)
        {
            for(int j =1;j<mNumColumns-1;++j)
            {
                mPrevSolution[i*mNumColumns+j].y = 
                mK1*mPrevSolution[i*mNumColumns+j].y +
                mK2*mCurrSolution[i*mNumColumns+j].y +
                mK3*(mCurrSolution[(i+1)*mNumColumns+j].y + 
                     mCurrSolution[(i-1)*mNumColumns+j].y + 
                     mCurrSolution[i*mNumColumns+j+1].y + 
                     mCurrSolution[i*mNumColumns+j-1].y);
                
            }

        });

        std::swap(mPrevSolution,mCurrSolution);

        t=0.0f;

        concurrency::parallel_for(1,mNumRows-1,[this](int i)
        {
            for(int j = 1; j < mNumColumns-1; ++j)
            {
                float l = mCurrSolution[i*mNumColumns+j-1].y;
                float r = mCurrSolution[i*mNumColumns+j+1].y;
                float t = mCurrSolution[(i-1)*mNumColumns+j].y;
                float b = mCurrSolution[(i+1)*mNumColumns+j].y;
                mNormals[i*mNumColumns+j].x = -r+l;
                mNormals[i*mNumColumns+j].y = 2.0f*mSpatialStep;
                mNormals[i*mNumColumns+j].z = b-t;

                XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&mNormals[i*mNumColumns+j]));
                XMStoreFloat3(&mNormals[i*mNumColumns+j], n);

                mTangentX[i*mNumColumns+j] = XMFLOAT3(2.0f*mSpatialStep, r-l, 0.0f);
                XMVECTOR T = XMVector3Normalize(XMLoadFloat3(&mTangentX[i*mNumColumns+j]));
                XMStoreFloat3(&mTangentX[i*mNumColumns+j], T);
            }
            
        });
        
    }
}

void Waves::Disturb(int i, int j, float magnitude)
{
    // Don't disturb boundaries.
    assert(i > 1 && i < mNumRows-2);
    assert(j > 1 && j < mNumColumns-2);

    float halfMag = 0.5f*magnitude;

    // Disturb the ijth vertex height and its neighbors.
    mCurrSolution[i*mNumColumns+j].y     += magnitude;
    mCurrSolution[i*mNumColumns+j+1].y   += halfMag;
    mCurrSolution[i*mNumColumns+j-1].y   += halfMag;
    mCurrSolution[(i+1)*mNumColumns+j].y += halfMag;
    mCurrSolution[(i-1)*mNumColumns+j].y += halfMag;
}
