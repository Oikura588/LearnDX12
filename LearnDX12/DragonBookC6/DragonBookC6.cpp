#include <array>

#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "../Common/DDSTextureLoader.h"
#include <DirectXColors.h>
#include <malloc.h>
#include <ppl.h>
#include <algorithm>
#include <vector>
#include <cassert>


using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

// FrameResources的数目.RenderItem和App都需要这个数据.
static const  int gNumFrameResources=3;
// 顶点信息
struct Vertex
{
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT2 TexCoord;
};
// 常量缓冲区.每个物体有自己的transform信息.
struct ObjectConstants
{
    XMFLOAT4X4 World = MathHelper::Identity4x4();
	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
};
// 材质常量缓冲区
struct MaterialConstants
{
    DirectX::XMFLOAT4 DiffuseAlbedo;
    DirectX::XMFLOAT3 FresnelR0;
    float Roughness;
    // 纹理贴图章节中用到
    DirectX::XMFLOAT4X4 MaterialTransform = MathHelper::Identity4x4();
};
// 存储绘制一个物体需要的数据的结构，随着不同的程序有所差别.
struct RenderItem
{
    RenderItem() = default;

    // 物体的世界Transform.用这个格式来存储，可以直接memcpy到buffer中.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

    // 用一个flag来计数，与FrameResource有关，每次判断这个值是否大于等于0来判断是否需要更新数据.
    int NumFramesDirty = gNumFrameResources;
    // 常量缓冲区中的偏移，如第10个物体就在缓冲区中第十个位置.
    UINT ObjCBOffset = -1;
    // 几何体的引用.几何体中存储了VertexBuffer和IndexBuffer.
    MeshGeometry* Geo = nullptr;

    // 材质
    Material* Mat = nullptr;
    // 图元类型
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    // DrawInstance 的参数
    UINT IndexCount;
    UINT StartIndexLocation;
    int BaseVertexLocation;
};

// 每次渲染时会更新的缓冲区。
struct PassConstants
{
    XMFLOAT4X4 View;    
    XMFLOAT4X4 InvView;
    XMFLOAT4X4 Proj;
    XMFLOAT4X4 InvProj;
    XMFLOAT4X4 ViewProj;
    XMFLOAT4X4 InvViewProj;

    XMFLOAT3 EyePosition;
    //float*4的内存对齐
    float    cbPad1;                
    XMFLOAT2 RenderTargetSize;
    XMFLOAT2 InvRenderTargetSize;
    float    NearZ;
    float    FarZ;
    float    TotalTime;
    float    DeltaTime;

    // 环境光
    XMFLOAT4 AmbientLight;
    // 直接光
    Light    Lights[MaxLights];
};

// 以CPU每帧都需更新的资源作为基本元素，包括CmdListAlloc、ConstantBuffer等.
// Draw()中进行绘制时，执行CmdList的Reset函数，来指定当前FrameResource所使用的CmdAlloc,从而将绘制命令存储在每帧的Alloc中.
class FrameResource
{
public:
    FrameResource(ID3D12Device* device,UINT passCount,UINT objectCount,UINT waveVertexCount,UINT materialCount)
    {
        device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(CmdAlloc.GetAddressOf())
        );
        ObjectsCB = std::make_unique<UploadBuffer<ObjectConstants>>(device,objectCount,true);
        PassCB = std::make_unique<UploadBuffer<PassConstants>>(device,passCount,true);
        mWavesVB = std::make_unique<UploadBuffer<Vertex>>(device, waveVertexCount,false);
        mMaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device,materialCount,true);
        
    }
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;

    // GPU处理完与此Allocator相关的命令前，不能对其重置，所以每帧都需要保存自己的Alloc.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdAlloc;
    
    // 在GPU处理完此ConstantBuffer相关的命令前，不能对其重置
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectsCB = nullptr;
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;

    // 波浪使用动态顶点缓冲区，类似上面的Constant Buffer.
    std::unique_ptr<UploadBuffer<Vertex>> mWavesVB = nullptr;
    
    // 材质的缓冲区
    std::unique_ptr<UploadBuffer<MaterialConstants>> mMaterialCB = nullptr;

    // 每帧需要有自己的fence，来判断GPU与CPU的帧之间的同步.
    UINT64 Fence = 0;
};

// 模拟波浪.
class Waves 
{
public:
    Waves(int m,int n,float dx,float dt,float speed,float damping);
    Waves(const Waves& rhs) = delete;
    Waves& operator=(const Waves& ) = delete;
    ~Waves();

    int RowCount();
    int ColumnCount();
    int VertexCount();
    int TriangleCount();
    float Width();
    float Depth();

    // return the solution at the ith grid point.
    const DirectX::XMFLOAT3& Position(int i) const {return mCurrSolution[i]; }
    const DirectX::XMFLOAT3& Normal(int i ) const {return mNormals[i]; }
    const DirectX:: XMFLOAT3& TangentX(int i) const {return mTangentX[i]; }

    void Update(float dt);
    void Disturb(int i,int j,float magnitude);
private:
    int mNumRows = 0;
    int mNumCols = 0;

    int mVertexCount = 0;
    int mTriangleCount = 0;

    // Simulate constants we can precompute.
    float mK1 = 0.0f;
    float mK2 = 0.0f;
    float mK3 = 0.0f;
   
    float mTimeStep = 0.0f;
    float mSpatialStep = 0.0f;

    std::vector<DirectX::XMFLOAT3> mPrevSolution;
    std::vector<DirectX::XMFLOAT3> mCurrSolution;
	std::vector<DirectX::XMFLOAT3> mNormals;
	std::vector<DirectX::XMFLOAT3> mTangentX;
};
Waves::Waves(int m, int n, float dx, float dt, float speed, float damping)
{
    mNumRows = m;
    mNumCols = n;

    mVertexCount = m*n;
    mTriangleCount=(m-1)*(n-1)*2;

    mTimeStep = dt;
    // dx应该是网格半径？
    mSpatialStep = dx;

    mPrevSolution.resize(mVertexCount);
    mCurrSolution.resize(mVertexCount);
    mNormals.resize(mVertexCount);
    mTangentX.resize(mVertexCount);

    // 更新参数
    // 思考，这里的参数是有什么特定的公式吗
	float d = damping * dt + 2.0f;
	float e = (speed * speed) * (dt * dt) / (dx * dx);
	mK1 = (damping * dt - 2.0f) / d;
	mK2 = (4.0f - 8.0f * e) / d;
	mK3 = (2.0f * e) / d;


    // 中心长度
    float halfWeight = (n-1)*dx/2.f;
    float halfHeight = (m-1)*dx/2.f;
    for (int i = 0; i < m; ++i)
    {
        float z = i*dx + (-halfHeight);
        for (int j = 0; j < n; ++j)
        {
            float x = j*dx + (-halfWeight);
            float idx = i*n+j;
            mPrevSolution[idx] = XMFLOAT3(x,0.f,z);
            mCurrSolution[idx] = XMFLOAT3(x,0.f,z);
            mNormals[idx] = XMFLOAT3(0.F,1.F,0.F);
            mTangentX[idx] = XMFLOAT3(1.f,0.f,0.f);
        }
    }
}
Waves::~Waves(){}

int Waves::RowCount()
{
    return mNumRows;
}

int Waves::ColumnCount()
{
    return mNumCols;
}

int Waves::VertexCount()
{
    return mNumRows*mNumCols;
}

int Waves::TriangleCount()
{
    return (mNumRows-1)*(mNumCols-1)*2;
}

float Waves::Width()
{
    return mSpatialStep *(mNumRows-1);
}

float Waves::Depth()
{
    return mSpatialStep *(mNumCols-1);
}

void Waves::Update(float dt)
{
	static float t = 0;

	// Accumulate time.
	t += dt;

	// Only update the simulation at the specified time step.
	if (t >= mTimeStep)
	{
		// Only update interior points; we use zero boundary conditions.
		//concurrency::parallel_for(1, mNumRows - 1, [this](int i)
			for(int i = 1; i < mNumRows-1; ++i)
			{
				for (int j = 1; j < mNumCols - 1; ++j)
				{
					// After this update we will be discarding the old previous
					// buffer, so overwrite that buffer with the new update.
					// Note how we can do this inplace (read/write to same element) 
					// because we won't need prev_ij again and the assignment happens last.

					// Note j indexes x and i indexes z: h(x_j, z_i, t_k)
					// Moreover, our +z axis goes "down"; this is just to 
					// keep consistent with our row indices going down.
					float newY = mK1 * mPrevSolution[i * mNumCols + j].y +
						mK2 * mCurrSolution[i * mNumCols + j].y +
						mK3 * (mCurrSolution[(i + 1) * mNumCols + j].y +
							mCurrSolution[(i - 1) * mNumCols + j].y +
							mCurrSolution[i * mNumCols + j + 1].y +
							mCurrSolution[i * mNumCols + j - 1].y);
                    float deltaY = newY - mPrevSolution[i*mNumCols+j].y;

					mPrevSolution[i * mNumCols + j].y = newY;
                    printf("%F",deltaY);
				}
			}
            //);

		// We just overwrote the previous buffer with the new data, so
		// this data needs to become the current solution and the old
		// current solution becomes the new previous solution.
		std::swap(mPrevSolution, mCurrSolution);

		t = 0.0f; // reset time

		//
		// Compute normals using finite difference scheme.
		//
		//concurrency::parallel_for(1, mNumRows - 1, [this](int i)
			for(int i = 1; i < mNumRows - 1; ++i)
			{
				for (int j = 1; j < mNumCols - 1; ++j)
				{
					float l = mCurrSolution[i * mNumCols + j - 1].y;
					float r = mCurrSolution[i * mNumCols + j + 1].y;
					float t = mCurrSolution[(i - 1) * mNumCols + j].y;
					float b = mCurrSolution[(i + 1) * mNumCols + j].y;
					mNormals[i * mNumCols + j].x = -r + l;
					mNormals[i * mNumCols + j].y = 2.0f * mSpatialStep;
					mNormals[i * mNumCols + j].z = b - t;

					XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&mNormals[i * mNumCols + j]));
					XMStoreFloat3(&mNormals[i * mNumCols + j], n);

					mTangentX[i * mNumCols + j] = XMFLOAT3(2.0f * mSpatialStep, r - l, 0.0f);
					XMVECTOR T = XMVector3Normalize(XMLoadFloat3(&mTangentX[i * mNumCols + j]));
					XMStoreFloat3(&mTangentX[i * mNumCols + j], T);
				}
			}
            //);
	}
}

void Waves::Disturb(int i, int j, float magnitude)
{
    // 边界不进行disturb
    assert(i>1 && j<mNumRows-2);
    assert(j>1&&j<mNumCols-2);

    // 指定点的周围四个顶点往上移动.
    float halfMag = 0.5*magnitude;
    mCurrSolution[i*mNumCols+j].y += magnitude;
    mCurrSolution[i*mNumCols+j+1].y += halfMag;
    mCurrSolution[i*mNumCols+j-1].y += halfMag;
    mCurrSolution[(i+1)*mNumCols+j].y +=halfMag;
    mCurrSolution[(i-1)*mNumCols+j].y += halfMag;
}

class BoxApp : public D3DApp
{
public:
    BoxApp(HINSTANCE hinstance);
    BoxApp(const BoxApp& rhs) = delete;
    BoxApp& operator=(const BoxApp& rhs) = delete;
    ~BoxApp();

    virtual bool Initialize() override;
private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

private:
   
    ComPtr<ID3D12RootSignature> mRootSignature  = nullptr;
	UINT mPassCbvOffset;

    // 存储所有渲染项.
    std::vector<std::unique_ptr<RenderItem>> mAllRenderItems;
    // 根据材质来划分渲染项.
    std::vector<RenderItem*> mOpaqueRenderItems;
    std::vector<RenderItem*> mTransparentRenderItems;

    // 所有渲染帧.
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrentFrameResource=nullptr;
    int mCurrentFrameIndex = 0;
    
    // MeshGeometry中包含了顶点、索引的buffer及view，方便管理几何体.
    std::unordered_map<std::string,std::unique_ptr<MeshGeometry>> mGeometries;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    // Shaders.
    std::unordered_map<std::string,ComPtr<ID3DBlob>> mShaders;

    // Pipeline state object.
    std::unordered_map<std::string,ComPtr<ID3D12PipelineState>> mPSOs;

    // 单个物体的常量缓冲区.
    XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();
    // Camera
    float mTheta = 1.5f*XM_PI;
    float mPhi = XM_PIDIV2-0.1f;
    float mRadius = 50.f;
    // 相机位置
    XMFLOAT3 mEyePos;

    POINT mLastMousePos;

    // 生成Land
    float GetHillsHeight(float x,float z) const;
    
    XMFLOAT3 GetHillsNormal(float x,float z) const;


    // 波浪
    std::unique_ptr<Waves> mWaves;

    // 保存render item的引用，每帧更新VB
	RenderItem* mWavesRitem = nullptr;

    // 是否开启线框模式
    bool mIsWireframe = false;

    // 材质
    std::unordered_map<std::string,std::unique_ptr<Material>> mMaterials;

    // 太阳位置
    float mSunTheta = 1.25f*XM_PI;
    float mSunPhi = XM_PIDIV4;

    // 纹理
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;

    // 采样器堆
    ComPtr<ID3D12DescriptorHeap> mSamplerHeap;
    // CBV SRV heap
    ComPtr<ID3D12DescriptorHeap> mSrvHeap;
};

BoxApp::BoxApp(HINSTANCE hinstance)
    :D3DApp(hinstance)
{
}

BoxApp::~BoxApp()
{
}

bool BoxApp::Initialize()
{
    // 初始化d3d,主要包括交换链、rtv、dsv
    if(!D3DApp::Initialize())
    {
        return false;
    }
    // 重置命令列表来执行初始化命令
    ThrowIfFailed( mCommandList->Reset(mDirectCmdListAlloc.Get(),nullptr));
    // 初始化.

    // 加载纹理
    {
        auto grassTex = std::make_unique<Texture>();
        grassTex->Name = "grassTex";
        grassTex->FileName = L"Textures\\grass.dds";
        
        ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),mCommandList.Get(),grassTex->FileName.c_str(),grassTex->Resource,grassTex->UploadHeap));

        auto waterTex = std::make_unique<Texture>();
        waterTex->Name = "waterTex";
        waterTex->FileName = L"Textures\\water1.dds";
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(), waterTex->FileName.c_str(), waterTex->Resource, waterTex->UploadHeap));

		auto fenceTex = std::make_unique<Texture>();
		fenceTex->Name = "fenceTex";
		fenceTex->FileName = L"Textures\\WireFence.dds";

		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(), fenceTex->FileName.c_str(), fenceTex->Resource, fenceTex->UploadHeap));

        mTextures[grassTex->Name] = std::move(grassTex);
        mTextures[waterTex->Name] = std::move(waterTex);
        mTextures[fenceTex->Name] = std::move(fenceTex);
    }
    // 创建采样器堆
    {
        D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
        samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        samplerHeapDesc.NumDescriptors = 1;
        samplerHeapDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;

        md3dDevice->CreateDescriptorHeap(&samplerHeapDesc,IID_PPV_ARGS(mSamplerHeap.GetAddressOf()));
    }
    // 创建采样器
    {
        D3D12_SAMPLER_DESC samplerDesc = {};
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.MipLODBias = 0;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

        md3dDevice->CreateSampler(&samplerDesc,mSamplerHeap->GetCPUDescriptorHandleForHeapStart());
    }
    // 创建Srv heap 以及srv
    {
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.NumDescriptors = mTextures.size();
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        md3dDevice->CreateDescriptorHeap(&srvHeapDesc,IID_PPV_ARGS(mSrvHeap.GetAddressOf()));

        auto grassTex = mTextures["grassTex"]->Resource;
		auto waterTex = mTextures["waterTex"]->Resource;
        auto fenceTex = mTextures["fenceTex"]->Resource;
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; //特殊用途
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;                      // 2D纹理的格式
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

       
        // 第一个
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle= mSrvHeap->GetCPUDescriptorHandleForHeapStart();
		srvDesc.Format = grassTex->GetDesc().Format;     //从纹理中读取格式
        srvDesc.Texture2D.MipLevels = grassTex->GetDesc().MipLevels;
		md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, srvHandle);
        
        // 创第二个
        srvHandle.ptr += mCbvUavDescriptorSize;
        srvDesc.Format = waterTex->GetDesc().Format;
        srvDesc.Texture2D.MipLevels = waterTex->GetDesc().MipLevels;
		md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, srvHandle);

        // 第三个
		srvHandle.ptr += mCbvUavDescriptorSize;
		srvDesc.Format = fenceTex->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = fenceTex->GetDesc().MipLevels;
		md3dDevice->CreateShaderResourceView(fenceTex.Get(), &srvDesc, srvHandle);

    }
    // 创建材质
    {
        auto grass = std::make_unique<Material>();
        grass->Name = "grass";
        grass->MatCBIndex = 0;
        grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        grass->FresnelR0 = XMFLOAT3(0.01f,0.01f,0.01f);
        grass->Roughness = 0.125f;
        grass->NumFramesDirty = gNumFrameResources;
        grass->DiffuseSrvHeapIndex = 0;

        auto water = std::make_unique<Material>();
        water->MatCBIndex = 1;
        water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
        water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
        water->Roughness = 0.0f;
        water->NumFramesDirty = gNumFrameResources;
        water->DiffuseSrvHeapIndex = 1;

        auto wirefence = std::make_unique<Material>();
        wirefence->MatCBIndex = 2;
        wirefence->DiffuseSrvHeapIndex = 2;
        wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
		wirefence->Roughness = 0.25f;
        wirefence->NumFramesDirty = gNumFrameResources;

        mMaterials["grass"] = std::move(grass);
        mMaterials["water"] = std::move(water);
        mMaterials["wirefence"] = std::move(wirefence);
        
    }
	// Build Geometry.和渲染没什么关系了，就是创建buffer并保存起来，绘制的时候用
	{
		// 使用工具函数创建顶点和索引的数组
		GeometryGenerator::MeshData grid = GeometryGenerator::CreateGrid(160.0f, 160.0f, 50, 50);
        GeometryGenerator::MeshData box = GeometryGenerator::CreateBox(8.0f,8.f,8.f,0);

		SubmeshGeometry gridSubmesh;
		gridSubmesh.BaseVertexLocation = 0;
		gridSubmesh.IndexCount = grid.Indices32.size();
		gridSubmesh.StartIndexLocation = 0;

        SubmeshGeometry boxSubmesh;
        boxSubmesh.BaseVertexLocation = grid.Vertices.size();
        boxSubmesh.StartIndexLocation = grid.Indices32.size();
        boxSubmesh.IndexCount = box.Indices32.size();


		std::vector<Vertex> vertices(grid.Vertices.size()+box.Vertices.size());
        int k=0;
        for (size_t i = 0; i < grid.Vertices.size(); ++i,++k)
        {
            auto& p = grid.Vertices[i].Position;
            vertices[k].Pos = p;
            vertices[k].Pos.y = GetHillsHeight(p.x,p.z);
            vertices[k].Normal = GetHillsNormal(p.x,p.z);
            vertices[k].TexCoord = grid.Vertices[i].TexC;
           
        }
		for (size_t i = 0; i < box.Vertices.size(); ++i,++k)
		{
			auto& p = box.Vertices[i].Position;
			vertices[k].Pos = p;
			vertices[k].Normal = box.Vertices[i].Normal;
			vertices[k].TexCoord = box.Vertices[i].TexC;
		}
        std::vector<std::uint16_t> indices = grid.GetIndices16();
        indices.insert(indices.end(),box.GetIndices16().begin(),box.GetIndices16().end());


		// 创建几何体和子几何体，存储绘制所用到的Index、Vertex信息
		auto mBoxGeo = std::make_unique<MeshGeometry>();
		mBoxGeo->Name = "shapeGeoTest";
		UINT vbByteSize = vertices.size() * sizeof(Vertex);
		UINT ibByteSize = indices.size() * sizeof(std::uint16_t);

		mBoxGeo->VertexByteStride = sizeof(Vertex);
		mBoxGeo->VertexBufferByteSize = vbByteSize;
		mBoxGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
		mBoxGeo->IndexBufferByteSize = ibByteSize;

		mBoxGeo->DrawArgs["grid"] = gridSubmesh;
        mBoxGeo->DrawArgs["box"] = boxSubmesh;
	

		// 创建顶点缓冲区.

		// 创建默认缓冲区资源.
		D3D12_HEAP_PROPERTIES defaultBufferProperties;
		defaultBufferProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		defaultBufferProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		defaultBufferProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		defaultBufferProperties.CreationNodeMask = 1;
		defaultBufferProperties.VisibleNodeMask = 1;
		D3D12_RESOURCE_DESC vbDesc;
		vbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		vbDesc.Alignment = 0;
		vbDesc.Format = DXGI_FORMAT_UNKNOWN;
		vbDesc.Height = 1;
		vbDesc.Width = vbByteSize;
		vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		vbDesc.MipLevels = 1;
		vbDesc.SampleDesc.Count = 1;
		vbDesc.SampleDesc.Quality = 0;
		vbDesc.DepthOrArraySize = 1;
		D3D12_RESOURCE_DESC ibDesc = vbDesc;
		ibDesc.Width = ibByteSize;

		// VB
		md3dDevice->CreateCommittedResource(
			&defaultBufferProperties,
			D3D12_HEAP_FLAG_NONE,
			&vbDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(mBoxGeo->VertexBufferGPU.GetAddressOf())
		);
		// IB
		md3dDevice->CreateCommittedResource(
			&defaultBufferProperties,
			D3D12_HEAP_FLAG_NONE,
			&ibDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(mBoxGeo->IndexBufferGPU.GetAddressOf())
		);
		// 创建作为中介的上传缓冲区.
		defaultBufferProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

		md3dDevice->CreateCommittedResource(
			&defaultBufferProperties,
			D3D12_HEAP_FLAG_NONE,
			&vbDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(mBoxGeo->VertexBufferUploader.GetAddressOf())
		);
		md3dDevice->CreateCommittedResource(
			&defaultBufferProperties,
			D3D12_HEAP_FLAG_NONE,
			&ibDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(mBoxGeo->IndexBufferUploader.GetAddressOf())
		);


		// 把默认的buffer改成写入状态来复制.
		mCommandList->ResourceBarrier(
			1,
			&CD3DX12_RESOURCE_BARRIER::Transition(
				mBoxGeo->VertexBufferGPU.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST
			)
		);
		mCommandList->ResourceBarrier(
			1,
			&CD3DX12_RESOURCE_BARRIER::Transition(
				mBoxGeo->IndexBufferGPU.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST
			)
		);



		// 把数据拷贝到上传缓冲区.使用Subresource.
		// 涉及到的调用:
		// ID3D12Device::GetCopyableFootprints.
		// UploaderBuffer::Map
		// memcpy()?
		// 思考，这里为什么不直接memcpy整个vertexbuffer，而是用subresource来处理.

		BYTE* pData;

		HRESULT hr = mBoxGeo->VertexBufferUploader->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		ThrowIfFailed(hr);
		UINT64 SrcOffset = 0;
		UINT64 NumBytes = 0;

		{
			memcpy(pData, vertices.data(), vbByteSize);
		}
		// Unmap.
		mBoxGeo->VertexBufferUploader->Unmap(0, nullptr);
		pData = nullptr;
		// copy buffer.
		mCommandList->CopyBufferRegion(
			mBoxGeo->VertexBufferGPU.Get(),
			0,
			mBoxGeo->VertexBufferUploader.Get(),
			SrcOffset,
			vbByteSize
		);

		// 同样的流程，复制index buffer.
		hr = mBoxGeo->IndexBufferUploader->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		ThrowIfFailed(hr);
		{
			memcpy(pData, indices.data(), ibByteSize);
		}
		mBoxGeo->IndexBufferUploader->Unmap(0, nullptr);
		pData = nullptr;
		// copubuffer
		mCommandList->CopyBufferRegion(
			mBoxGeo->IndexBufferGPU.Get(),
			0,
			mBoxGeo->IndexBufferUploader.Get(),
			0,
			ibByteSize

		);


		// Copy结束后，改变buffer状态
		mCommandList->ResourceBarrier(
			1,
			&CD3DX12_RESOURCE_BARRIER::Transition(
				mBoxGeo->VertexBufferGPU.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ
			)
		);
		mCommandList->ResourceBarrier(
			1,
			&CD3DX12_RESOURCE_BARRIER::Transition(
				mBoxGeo->IndexBufferGPU.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ
			)
		);

        mGeometries["landGeo"]=std::move(mBoxGeo);


        // 创建 waves 的索引缓冲区
        mWaves = std::make_unique<Waves>(64,64,1.f,0.03f,4.f,0.2f);
        indices.resize(3 * mWaves->TriangleCount());
        //std::vector<std::uint16_t> indices();
        int m = mWaves->RowCount();
        int n = mWaves->ColumnCount();
        k = 0;
        for (int i = 0; i < m - 1; ++i)
        {
            for (int j = 0; j < n - 1; ++j)
            {
                indices[k] = i*n+j;
                indices[k+1] = i*n+j+1;
                indices[k+2] = (i+1)*n+j;

                indices[k+3] = i*n+j+1;
                indices[k+4] = (i+1)*n+j+1;
                indices[k+5] = (i+1)*n+j;
                k+=6;
            }
        }

        SubmeshGeometry submesh;
        submesh.BaseVertexLocation = 0;
        submesh.StartIndexLocation = 0;
        submesh.IndexCount = indices.size();


        vbByteSize = mWaves->VertexCount()*sizeof(Vertex);
        ibByteSize = indices.size()*sizeof(std::uint16_t);

        auto mWaveGeo = std::make_unique<MeshGeometry>();
        mWaveGeo->Name = "Waves";
		mWaveGeo->DrawArgs["grid"] = submesh;

        mWaveGeo->VertexBufferCPU = nullptr;
        mWaveGeo->VertexBufferGPU = nullptr;
        mWaveGeo->IndexBufferCPU = nullptr;
        
        mWaveGeo->VertexBufferByteSize = vbByteSize;
        mWaveGeo->VertexByteStride = sizeof(Vertex);
        mWaveGeo->IndexBufferByteSize = ibByteSize;
        mWaveGeo->IndexFormat = DXGI_FORMAT_R16_UINT;

        // 创建index buffer
        ibDesc.Width = ibByteSize;
        defaultBufferProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

        // 创建IB的实际buffer
        md3dDevice->CreateCommittedResource(
            &defaultBufferProperties,
            D3D12_HEAP_FLAG_NONE,
            &ibDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(mWaveGeo->IndexBufferGPU.GetAddressOf())
        );
        // 创建IB的中介buffer
        defaultBufferProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

        md3dDevice->CreateCommittedResource(
            &defaultBufferProperties,
            D3D12_HEAP_FLAG_NONE,
            &ibDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(mWaveGeo->IndexBufferUploader.GetAddressOf())  
        );

        // 把索引缓冲区拷贝到中介区.
        pData = nullptr;
        mWaveGeo->IndexBufferUploader->Map(0,nullptr, (void**)&pData);
        memcpy(pData,indices.data(),ibByteSize);
        mWaveGeo->IndexBufferUploader->Unmap(0,nullptr);
        pData = nullptr;
        // 把上传区的数据拷贝的default
        mCommandList->ResourceBarrier(
            1,
            &CD3DX12_RESOURCE_BARRIER::Transition(mWaveGeo->IndexBufferGPU.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST)
        );

        mCommandList->CopyBufferRegion(
            mWaveGeo->IndexBufferGPU.Get(),0,mWaveGeo->IndexBufferUploader.Get(),0,ibByteSize
        );
		mCommandList->ResourceBarrier(
			1,
			&CD3DX12_RESOURCE_BARRIER::Transition(mWaveGeo->IndexBufferGPU.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ)
		);
        mGeometries["waveGeo"] = std::move(mWaveGeo);
	}


	// 构建几何体后就可以Build渲染项了，渲染项可以理解为是几何体的实例化，每个渲染项是场景中的一个物体，比如可能有多个圆台形组成的物体
	{
		auto landRenderItem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&landRenderItem->World, XMMatrixScaling(2.F, 2.F, 2.F)* XMMatrixTranslation(0., 0.5f, 0.0f));
		landRenderItem->ObjCBOffset = 0;
		landRenderItem->Geo = mGeometries["landGeo"].get();
		landRenderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		landRenderItem->BaseVertexLocation = landRenderItem->Geo->DrawArgs["grid"].BaseVertexLocation;
		landRenderItem->StartIndexLocation = landRenderItem->Geo->DrawArgs["grid"].StartIndexLocation;
		landRenderItem->IndexCount = landRenderItem->Geo->DrawArgs["grid"].IndexCount;
        landRenderItem->Mat = mMaterials["grass"].get();
        XMStoreFloat4x4(&landRenderItem->Mat->MatTransform,XMMatrixScaling(5.f,5.f,1.f));

		mAllRenderItems.push_back(std::move(landRenderItem));

		auto boxRenderItem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&boxRenderItem->World, XMMatrixScaling(2.F, 2.F, 2.F)* XMMatrixTranslation(0., 0.5f, 0.0f));
		boxRenderItem->ObjCBOffset = 0;
		boxRenderItem->Geo = mGeometries["landGeo"].get();
		boxRenderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		boxRenderItem->BaseVertexLocation = boxRenderItem->Geo->DrawArgs["box"].BaseVertexLocation;
		boxRenderItem->StartIndexLocation = boxRenderItem->Geo->DrawArgs["box"].StartIndexLocation;
		boxRenderItem->IndexCount = boxRenderItem->Geo->DrawArgs["box"].IndexCount;
		boxRenderItem->Mat = mMaterials["wirefence"].get();
		XMStoreFloat4x4(&boxRenderItem->Mat->MatTransform, XMMatrixScaling(2.f, 2.f, 2.f));

		mAllRenderItems.push_back(std::move(boxRenderItem));

        auto waveRenderItem = std::make_unique<RenderItem>();
        waveRenderItem->World = MathHelper::Identity4x4();
        waveRenderItem->ObjCBOffset = 1;
        waveRenderItem->Geo = mGeometries["waveGeo"].get();
        waveRenderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        waveRenderItem->BaseVertexLocation = waveRenderItem->Geo->DrawArgs["grid"].BaseVertexLocation;
		waveRenderItem->StartIndexLocation = waveRenderItem->Geo->DrawArgs["grid"].StartIndexLocation;
		waveRenderItem->IndexCount = waveRenderItem->Geo->DrawArgs["grid"].IndexCount;
        mWavesRitem = waveRenderItem.get();
        waveRenderItem->Mat = mMaterials["water"].get();
		//XMStoreFloat4x4(&waveRenderItem->Mat->MatTransform, XMMatrixScaling(5.f, 5.f, 1.f));

        mAllRenderItems.push_back(std::move(waveRenderItem));

		// 非透明，添加到对应Pass中
		for (auto& e : mAllRenderItems)
		{
			mOpaqueRenderItems.push_back(e.get());
		}
	} 
 
    // 初始化多个帧资源.需要给按照物体个数初始化常量缓冲区，所以需要事先知道物体个数.
    {
        for(int i=0;i<gNumFrameResources;++i)
        {
            mFrameResources.push_back(
                std::make_unique<FrameResource>(md3dDevice.Get(),1,mAllRenderItems.size(),mWaves->VertexCount(), mMaterials.size()));
        }
    }
	// 不再创建常量缓冲区堆，直接绑定CBV（即直接用根描述符而非描述符表） 
	{
		
	}
    
    // 初始化常量缓冲区View.
    {
        // 不需要与cbvHeap绑定，需要时直接设置BufferLocation即可
    }
    
    // 初始化RootSignature，把常量缓冲区绑定到GPU上供Shader读取.
    // 把着色器当作一个函数，root signature就是函数签名，资源是参数数据.
    {
        // root signature -> root parameter -> type/table -> range.
        
        D3D12_ROOT_PARAMETER slotRootParameter[5];
        // object 
        slotRootParameter[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        slotRootParameter[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        slotRootParameter[0].Descriptor.RegisterSpace = 0;
        slotRootParameter[0].Descriptor.ShaderRegister = 0;
        // 暂时不需要填充Descriptor，绘制的时候设置即可.

		// 材质
        slotRootParameter[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        slotRootParameter[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        slotRootParameter[1].Descriptor.RegisterSpace = 0;
        slotRootParameter[1].Descriptor.ShaderRegister = 1;

        // pass
        slotRootParameter[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        slotRootParameter[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		slotRootParameter[2].Descriptor.RegisterSpace = 0;
		slotRootParameter[2].Descriptor.ShaderRegister = 2;
		// 暂时不需要填充Descriptor，绘制的时候设置即可.

        // srv
		D3D12_DESCRIPTOR_RANGE srvRange;
		srvRange.BaseShaderRegister = 0;
		srvRange.NumDescriptors = 1;
		srvRange.OffsetInDescriptorsFromTableStart = 0;
		srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		srvRange.RegisterSpace = 0;

		D3D12_ROOT_DESCRIPTOR_TABLE srvTable;
		srvTable.NumDescriptorRanges = 1;
		srvTable.pDescriptorRanges = &srvRange;

        slotRootParameter[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        slotRootParameter[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        slotRootParameter[3].DescriptorTable = srvTable;

        // Sampler
        D3D12_DESCRIPTOR_RANGE samplerRange;
        samplerRange.BaseShaderRegister = 0;
        samplerRange.NumDescriptors = 1;
        samplerRange.OffsetInDescriptorsFromTableStart = 0;
        samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        samplerRange.RegisterSpace = 0;
        
        D3D12_ROOT_DESCRIPTOR_TABLE samplerTable;
        samplerTable.NumDescriptorRanges = 1;
        samplerTable.pDescriptorRanges = &samplerRange;
        // 思考:采样器只能用表?
        slotRootParameter[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        slotRootParameter[4].DescriptorTable = samplerTable;
        slotRootParameter[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        
        // 创建RootSignature.要使用Blob
        // d3d12规定，必须将根签名的描述布局进行序列化，才可以传入CreateRootSignature方法.
        ComPtr<ID3DBlob> serializedBlob = nullptr,errBlob = nullptr;
        
        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        rootSignatureDesc.NumParameters = 5;
        rootSignatureDesc.pParameters = slotRootParameter;
        rootSignatureDesc.pStaticSamplers = nullptr;
        rootSignatureDesc.NumStaticSamplers = 0;
        
        HRESULT hr =  D3D12SerializeRootSignature(&rootSignatureDesc,D3D_ROOT_SIGNATURE_VERSION_1,serializedBlob.GetAddressOf(),errBlob.GetAddressOf());
		ThrowIfFailed(hr);
        md3dDevice->CreateRootSignature(
            0,
            serializedBlob->GetBufferPointer(),
            serializedBlob->GetBufferSize(),
            IID_PPV_ARGS(mRootSignature.GetAddressOf())
        );


        // 绘制时调用的逻辑，设置根签名以及使用的描述符资源，本例中使用描述符表。描述符表存在描述符堆中.
        //mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
        // 使用根描述符,就不需要设置Heap了，直接设置CBV即可
	   /* mCommandList->SetGraphicsRootConstantBufferView(
			0,
			cbv
		);*/
    }

    // 编译着色器.
    {
       
        std::wstring filename;
        std::string entrypoint,target;
        UINT compileFlags = 0;
#if defined(DEBUG)|defined(_DEBUG)
        compileFlags =  D3DCOMPILE_DEBUG|D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        HRESULT hr = S_OK;
        ComPtr<ID3DBlob> errorCode = nullptr;
        
        // 编译vs
        filename = L"Shaders\\color.hlsl";
        entrypoint = "VS";
        target = "vs_5_0";
        mShaders["vShader"] = nullptr;
        mShaders["pShader"] = nullptr;
        
        hr = D3DCompileFromFile(
            filename.c_str(),
            nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entrypoint.c_str(),
            target.c_str(),
            compileFlags,
            0,
            &mShaders["vShader"],
            &errorCode
        );
        if(errorCode!=nullptr)
        {
            OutputDebugStringA((char*)errorCode->GetBufferPointer());
        }
        ThrowIfFailed(hr);

        errorCode = nullptr;
        hr = S_OK;
        // 编译ps.
        filename = L"Shaders\\color.hlsl";
        entrypoint = "PS";
        target = "ps_5_0";
        hr = D3DCompileFromFile(
          filename.c_str(),
          nullptr,
          D3D_COMPILE_STANDARD_FILE_INCLUDE,
          entrypoint.c_str(),
          target.c_str(),
          compileFlags,
          0,
          &mShaders["pShader"],
          &errorCode
      );
        if(errorCode!=nullptr)
        {
            OutputDebugStringA((char*)errorCode->GetBufferPointer());
        }
        ThrowIfFailed(hr);
    }
    // 输入布局
    {
        mInputLayout = {
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0,},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
        };
    }

    // PSO
    {
        // BlendState.
        D3D12_BLEND_DESC blendDesc;
        blendDesc.AlphaToCoverageEnable = false;
        blendDesc.IndependentBlendEnable = false;
        D3D12_RENDER_TARGET_BLEND_DESC rtDesc={false,false,D3D12_BLEND_ONE,D3D12_BLEND_ZERO,D3D12_BLEND_OP_ADD,D3D12_BLEND_ONE,D3D12_BLEND_ZERO,D3D12_BLEND_OP_ADD,D3D12_LOGIC_OP_NOOP,D3D12_COLOR_WRITE_ENABLE_ALL};
        
        for(int i =0;i<D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;++i)
        {
            blendDesc.RenderTarget[i]=rtDesc;
        }

        
        // 光栅化状态
        D3D12_RASTERIZER_DESC rasterizerDesc;
        rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        // 关闭剔除，剔除正面，剔除背面
        rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
        rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        // 渲染模式，是否启用线框模式渲染.
        rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
        rasterizerDesc.MultisampleEnable = FALSE;
        rasterizerDesc.FrontCounterClockwise = FALSE;
        rasterizerDesc.AntialiasedLineEnable = FALSE;
        rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        rasterizerDesc.DepthClipEnable = TRUE;
        rasterizerDesc.ForcedSampleCount = 0;
        rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;

        // DepthStencilState.
        D3D12_DEPTH_STENCIL_DESC dsDesc;
        dsDesc.DepthEnable = true;
        dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        dsDesc.StencilEnable = false;
        dsDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        dsDesc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

        const D3D12_DEPTH_STENCILOP_DESC dsopDesc = {
            D3D12_STENCIL_OP_KEEP,
            D3D12_STENCIL_OP_KEEP,
            D3D12_STENCIL_OP_KEEP,
            D3D12_COMPARISON_FUNC_ALWAYS
        };
        dsDesc.FrontFace = dsopDesc;
        dsDesc.BackFace = dsopDesc;
        

        
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc;
        ZeroMemory(&pipelineStateDesc,sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
        pipelineStateDesc.pRootSignature = mRootSignature.Get();

        // 把blob转为Shader bytecode.
        pipelineStateDesc.VS  =
            {
            reinterpret_cast<BYTE*>(mShaders["vShader"]->GetBufferPointer()),mShaders["vShader"]->GetBufferSize()
            };
        pipelineStateDesc.PS =
            {
            reinterpret_cast<BYTE*>(mShaders["pShader"]->GetBufferPointer()),mShaders["pShader"]->GetBufferSize()
            };
        pipelineStateDesc.BlendState = blendDesc;
        // 一般全部开启.
        pipelineStateDesc.SampleMask = UINT_MAX;
        pipelineStateDesc.RasterizerState = rasterizerDesc;
        pipelineStateDesc.DepthStencilState = dsDesc;
        pipelineStateDesc.InputLayout = {mInputLayout.data(),(UINT)mInputLayout.size()};
        pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipelineStateDesc.NumRenderTargets = 1;
        pipelineStateDesc.RTVFormats[0] = mBackBufferFormat;
        pipelineStateDesc.DSVFormat = mDepthStencilFormat;
        pipelineStateDesc.SampleDesc.Count = 1;
        pipelineStateDesc.SampleDesc.Quality = 0;

        ThrowIfFailed( md3dDevice->CreateGraphicsPipelineState(
            &pipelineStateDesc,IID_PPV_ARGS(&mPSOs["opaquePSO"])
        ));

        // 创建线框模式pso.

        rasterizerDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;
        pipelineStateDesc.RasterizerState = rasterizerDesc;
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
			&pipelineStateDesc, IID_PPV_ARGS(&mPSOs["wireframePSO"])
		));
        // 创建透明的PSO
        D3D12_RENDER_TARGET_BLEND_DESC rtBlendDesc = {};
        rtBlendDesc.BlendEnable = true;
        rtBlendDesc.LogicOpEnable = false;
        rtBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        rtBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        rtBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
        rtBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
        rtBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
        rtBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rtBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
        rtBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        
        blendDesc;
        blendDesc.AlphaToCoverageEnable = false;
        blendDesc.IndependentBlendEnable = false;
        blendDesc.RenderTarget[0] = rtBlendDesc;
        rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
		pipelineStateDesc.RasterizerState = rasterizerDesc;
        pipelineStateDesc.BlendState = blendDesc;
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
			&pipelineStateDesc, IID_PPV_ARGS(&mPSOs["transparentPSO"])
		));



        
    }

    
    // 执行初始化命令
    mCommandList->Close();
    ID3D12CommandList* cmdLists[] = {mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(_countof(cmdLists),cmdLists);

    // 等待初始化完成.
    FlushCommandQueue();
    return true;
}

void BoxApp::OnResize()
{
    D3DApp::OnResize();

    // 更新纵横比、重算投影矩阵.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25*MathHelper::Pi,AspectRatio(),1.0f,1000.0f);
    XMStoreFloat4x4(&mProj,P);
}


void BoxApp::Update(const GameTimer& gt)
{
    // 更新键盘输入
    {
        if (GetAsyncKeyState('1') & 0x8000)
        {
            mIsWireframe = true;
        }
        else
        {
            mIsWireframe = false;
        }


        const float dt = gt.DeltaTime();
        if (GetAsyncKeyState(VK_LEFT) && 0x8000)
        {
            mSunTheta -=1.0f*dt;
        }
		if (GetAsyncKeyState(VK_RIGHT) && 0x8000)
		{
			mSunTheta += 1.0f * dt;
		}
		if (GetAsyncKeyState(VK_UP) && 0x8000)
		{
			mSunPhi -= 1.0f * dt;
		}
		if (GetAsyncKeyState(VK_DOWN) && 0x8000)
		{
			mSunPhi += 1.0f * dt;
		}
        mSunPhi = MathHelper::Clamp(mSunPhi,0.1f,XM_PIDIV2);
    }
    // 更新相机
    {
		// 更新相机位置
		float x = mRadius * sinf(mPhi) * cosf(mTheta);
		float z = mRadius * sinf(mPhi) * sinf(mTheta);
		float y = mRadius * cosf(mPhi);

		// View matrix.
		XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
		XMStoreFloat3(&mEyePos, pos);
		XMVECTOR target = XMVectorZero();
		XMVECTOR up = XMVectorSet(0.f, 1, 0.f, 0.f);
		XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
		XMStoreFloat4x4(&mView, view);
    }
    // 更新波浪的材质transform
    {
        auto waterMat = mMaterials["water"].get();
        float tu = waterMat->MatTransform(3,0);
        float tv = waterMat->MatTransform(3,1);
        
        tu += 0.1f *gt.DeltaTime();
        tv += 0.02 *gt.DeltaTime();
        
        if(tu>=1.0f)
            tu-=1.0f;
        if(tv>=1.0f)
            tv-=1.0f;
        waterMat->MatTransform(3,0) = tu;
        waterMat->MatTransform(3,1) = tv;
        waterMat->NumFramesDirty = gNumFrameResources;
    }

    // 循环获取FrameResource
    // 只有CPU比GPU快很多的情况需要特殊处理，其他情况下如果CPU很慢，每次Update后GPU都能直接Draw完成开始下次Update
    // 这种情况下需要让CPU等待GPU.
    mCurrentFrameIndex = (mCurrentFrameIndex+1)%gNumFrameResources;
    mCurrentFrameResource = mFrameResources[mCurrentFrameIndex].get();

    // 初始时都是0，所以初始时不需要比较
    if(mFence->GetCompletedValue()<mCurrentFrameResource->Fence &&mCurrentFrameResource->Fence!=0)
    {
        // 创建一个空的handle回调，用来阻塞程序直到触发fence点
        HANDLE eventHandle = CreateEventEx(nullptr,false,false,EVENT_ALL_ACCESS);
        mFence->SetEventOnCompletion(mCurrentFrameResource->Fence,eventHandle);
        WaitForSingleObject(eventHandle,INFINITE);
        CloseHandle(eventHandle);
    }

    // 更新FrameResource内的资源.

    // 更新物体的ConstantBuffer.每帧会判断物体的标记，如果为脏则更新
    {
        auto currObjectCB = mCurrentFrameResource->ObjectsCB.get();
        for(auto& e:mAllRenderItems)
        {
            // 当标记为脏时更新.
            if(e->NumFramesDirty>0)
            {
                XMMATRIX world = XMLoadFloat4x4(&e->World);
                ObjectConstants objConstants;
                XMStoreFloat4x4(&objConstants.World,XMMatrixTranspose(world));
                currObjectCB->CopyData(e->ObjCBOffset,objConstants);
                e->NumFramesDirty--;
            }
            
        }
    }
    //  更新Pass的CB.
    {
        XMMATRIX view = XMLoadFloat4x4(&mView);
        XMMATRIX proj = XMLoadFloat4x4(&mProj);
        XMMATRIX viewProj = XMMatrixMultiply(view,proj);
        XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj),viewProj);
        auto currPassCB = mCurrentFrameResource->PassCB.get();
        PassConstants passConstants;
        
        XMStoreFloat4x4(&passConstants.View,XMMatrixTranspose(view));
        XMStoreFloat4x4(&passConstants.InvView,XMMatrixTranspose(XMMatrixInverse(&XMMatrixDeterminant(view),view)));
        XMStoreFloat4x4(&passConstants.Proj,XMMatrixTranspose(proj));
        XMStoreFloat4x4(&passConstants.InvProj,XMMatrixTranspose(XMMatrixInverse(&XMMatrixDeterminant(proj),proj)));
        XMStoreFloat4x4(&passConstants.ViewProj,XMMatrixTranspose(viewProj));
        XMStoreFloat4x4(&passConstants.InvViewProj,XMMatrixTranspose(invViewProj));
        passConstants.RenderTargetSize = XMFLOAT2((float)mClientWidth,(float)mClientHeight);
        passConstants.InvRenderTargetSize = XMFLOAT2(1/(float)mClientWidth,1/(float)mClientHeight);
        passConstants.EyePosition = mEyePos;
        passConstants.NearZ = 1.0f;
        passConstants.FarZ = 1000.0f;
        passConstants.DeltaTime=gt.DeltaTime();
        passConstants.TotalTime = gt.TotalTime();

        // 光照
        passConstants.AmbientLight = {0.5f,0.5f,0.5f,1.f};
        XMFLOAT3 Direction;
        Direction.x = sinf(mSunPhi)*cosf(mSunTheta);
		Direction.y = cosf(mSunPhi);
		Direction.z = sinf(mSunPhi) *sinf(mSunTheta);
        passConstants.Lights[0].Direction = Direction;
		passConstants.Lights[0].Strength = XMFLOAT3(1.F, 1.F, 0.9F);


        currPassCB->CopyData(0,passConstants);
    }
    
    // 更新 waves 的VB
    {
        // 每隔一段时间，生成一个随机的波浪
        static float t_base = 0.0f;
        if ((mTimer.TotalTime() - t_base) >= 0.25)
        {
            t_base+=0.25;
            int i = MathHelper::Rand(4,mWaves->RowCount()-5);
            int j = MathHelper::Rand(4,mWaves->ColumnCount()-5);

            float r = MathHelper::RandF(0.2,0.5f);
            mWaves->Disturb(i,j,r);
            
        }

        // 模拟波浪计算
        mWaves->Update(gt.DeltaTime());
        // 更新顶点缓冲区.
		auto currWaveVB = mCurrentFrameResource->mWavesVB.get();
		for (int i = 0; i < mWaves->VertexCount(); ++i)
		{
			Vertex v;
			v.Pos = mWaves->Position(i);
			v.Normal = mWaves->Normal(i);

            // u的坐标可以看作是从[-w/2,w/2]映射到[0,1]
            v.TexCoord.x = v.Pos.x/mWaves->Width() + 0.5f;
            v.TexCoord.y = -v.Pos.z/mWaves->Depth()+0.5f;
			currWaveVB->CopyData(i, v);

		}
		mWavesRitem->Geo->VertexBufferGPU = currWaveVB->Resource();
    }
    // 更新材质
    {
        auto currMaterialCB = mCurrentFrameResource->mMaterialCB.get();
        for (auto& e : mMaterials)
        {
            Material* mat = e.second.get();
            if(mat->NumFramesDirty>0)
            {
                MaterialConstants matConstants;
                matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
                matConstants.FresnelR0 = mat->FresnelR0;
                
                XMStoreFloat4x4(&matConstants.MaterialTransform , XMMatrixTranspose(XMLoadFloat4x4(&mat->MatTransform)));
                matConstants.Roughness = mat->Roughness;

                currMaterialCB->CopyData(mat->MatCBIndex,matConstants);
                mat->NumFramesDirty--;
            }
        }
    }

}

void BoxApp::Draw(const GameTimer& gt)
{
    //FlushCommandQueue();
    // cmd相关Reset
    auto mCurrentAlloc = mCurrentFrameResource->CmdAlloc;
    mCurrentAlloc->Reset();   // cmdlist 执行完后才能重置,即FlushCommandQuene之后.
    if (mIsWireframe)
    {
        mCommandList->Reset(mCurrentAlloc.Get(),mPSOs["wireframePSO"].Get());
    }
    else
    { 
        mCommandList->Reset(mCurrentAlloc.Get(),mPSOs["opaquePSO"].Get());  // 传入Queue后就可以重置.
    }
    mCommandList->RSSetViewports(1,&mScreenViewport);
    mCommandList->RSSetScissorRects(1,&mScissorRect);

    // 转换资源视图，准备开始绘制.
    mCommandList->ResourceBarrier(
        1,&CD3DX12_RESOURCE_BARRIER::Transition(
            CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        )
    );

    // 清除状态
    mCommandList->ClearRenderTargetView(CurrentBackBufferDescriptor(),Colors::LightSteelBlue,0,nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilDescriptor(),D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,1.0f,0,0,nullptr);

    // 指定渲染视图
    mCommandList->OMSetRenderTargets(1,&CurrentBackBufferDescriptor(),true,&DepthStencilDescriptor());

    // 描述符相关.用来更新常量缓冲区
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    // 设置采样器堆和采样器
	ID3D12DescriptorHeap* descriptorHeap[] = { mSamplerHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeap), descriptorHeap);

	// 设置采样器
	mCommandList->SetGraphicsRootDescriptorTable(4, mSamplerHeap->GetGPUDescriptorHandleForHeapStart());

    // 设置常量堆
	ID3D12DescriptorHeap* srvDescriptorHeap[] = { mSrvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(srvDescriptorHeap), srvDescriptorHeap);

    int passCbvIndex = mPassCbvOffset + mCurrentFrameIndex;
    mCommandList->SetGraphicsRootConstantBufferView(2,mCurrentFrameResource->PassCB->Resource()->GetGPUVirtualAddress());

    // 绘制一个物体需要绑定两个buffer、设置图元类型、设置常量缓冲区等，把这些绘制一个物体需要的数据整合起来，可以作为RenderItem.
	// 绘制物体
	{
		UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
		UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

		auto objCB = mCurrentFrameResource->ObjectsCB->Resource();

        // 最后一个是水，先渲染不透明物体
        mCommandList->SetPipelineState(mPSOs["opaquePSO"].Get());
		for (size_t i = 0; i < mOpaqueRenderItems.size()-1; ++i)
		{
			auto ri = mOpaqueRenderItems[i];
            // 更新object常量
			mCommandList->IASetVertexBuffers(0,1,&ri->Geo->VertexBufferView());
			mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
			mCommandList->IASetPrimitiveTopology(ri->PrimitiveTopology);
            D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mCurrentFrameResource->ObjectsCB->Resource()->GetGPUVirtualAddress();
            cbAddress+=(ri->ObjCBOffset )*objCBByteSize;
			mCommandList->SetGraphicsRootConstantBufferView(0, cbAddress);
			// 设置材质
            D3D12_GPU_VIRTUAL_ADDRESS matAddress = mCurrentFrameResource->mMaterialCB->Resource()->GetGPUVirtualAddress();
            matAddress+=(ri->Mat->MatCBIndex)* matCBByteSize;
            mCommandList->SetGraphicsRootConstantBufferView(1,matAddress);

            //D3D12_GPU_VIRTUAL_ADDRESS texAddress;
            //if (ri->Mat->DiffuseSrvHeapIndex == 0)
            //{
            //    texAddress = mTextures["grassTex"]->Resource->GetGPUVirtualAddress();
            //}
            //else if(ri->Mat->DiffuseSrvHeapIndex == 1)
            //{
            //    texAddress =mTextures["waterTex"]->Resource->GetGPUVirtualAddress();

            //}
			D3D12_GPU_DESCRIPTOR_HANDLE texAddress = mSrvHeap->GetGPUDescriptorHandleForHeapStart();
            texAddress.ptr += (ri->Mat->DiffuseSrvHeapIndex)*mCbvUavDescriptorSize;

            // 设置纹理
             
            mCommandList->SetGraphicsRootDescriptorTable(3, texAddress);


			mCommandList->DrawIndexedInstanced(ri->IndexCount,1,ri->StartIndexLocation,ri->BaseVertexLocation,0);
            //mCommandList->DrawIndexedInstanced(3,1,0,0,0);
		}

        // 渲染最后的水
		mCommandList->SetPipelineState(mPSOs["transparentPSO"].Get());
		auto ri = mOpaqueRenderItems[mOpaqueRenderItems.size() - 1];
		// 更新object常量
		mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		mCommandList->IASetPrimitiveTopology(ri->PrimitiveTopology);
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mCurrentFrameResource->ObjectsCB->Resource()->GetGPUVirtualAddress();
		cbAddress += (ri->ObjCBOffset) * objCBByteSize;
		mCommandList->SetGraphicsRootConstantBufferView(0, cbAddress);
		// 设置材质
		D3D12_GPU_VIRTUAL_ADDRESS matAddress = mCurrentFrameResource->mMaterialCB->Resource()->GetGPUVirtualAddress();
		matAddress += (ri->Mat->MatCBIndex) * matCBByteSize;
		mCommandList->SetGraphicsRootConstantBufferView(1, matAddress);

		//D3D12_GPU_VIRTUAL_ADDRESS texAddress;
		//if (ri->Mat->DiffuseSrvHeapIndex == 0)
		//{
		//    texAddress = mTextures["grassTex"]->Resource->GetGPUVirtualAddress();
		//}
		//else if(ri->Mat->DiffuseSrvHeapIndex == 1)
		//{
		//    texAddress =mTextures["waterTex"]->Resource->GetGPUVirtualAddress();

		//}
		D3D12_GPU_DESCRIPTOR_HANDLE texAddress = mSrvHeap->GetGPUDescriptorHandleForHeapStart();
		texAddress.ptr += (ri->Mat->DiffuseSrvHeapIndex) * mCbvUavDescriptorSize;

		// 设置纹理

		mCommandList->SetGraphicsRootDescriptorTable(3, texAddress);


		mCommandList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);

	}

    // 绘制完成后改变资源状态.
    mCommandList->ResourceBarrier(
        1,&CD3DX12_RESOURCE_BARRIER::Transition(
            CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT
        )
    );
    mCommandList->Close();

    ID3D12CommandList* cmdLists[] = {mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(_countof(cmdLists),cmdLists);

    ThrowIfFailed(mSwapChain->Present(0,0));
    mCurrBackBuffer = (mCurrBackBuffer+1)%SwapChainBufferCount;

    // 增加Fence
    mCurrentFence++;
    mCurrentFrameResource->Fence = mCurrentFence;
    mCommandQueue->Signal(mFence.Get(),mCurrentFence);
}

void BoxApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void BoxApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void BoxApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.2 unit in the scene.
		float dx = 0.2f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.2f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}


float BoxApp::GetHillsHeight(float x, float z) const
{
    return 0.3f*(z*sinf(0.1f*x)+x*cosf(0.1f*z));
}

XMFLOAT3 BoxApp::GetHillsNormal(float x, float z) const
{
    // n= (-df/dx,1,-df/dz)
    XMFLOAT3 n(
        -0.03f*z*cosf(0.1f*x) - 0.3f*cosf(0.1f*z),
        1.0f,
        -0.3f*sinf(0.1f*x) + 0.03f*x*sinf(0.1f*z)
    );

    XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n,unitNormal);

    return n;
}

int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE prevInstance,PSTR cmdLine,int showCmd)
{
    // Debug下开启内存检测，内存泄漏情况
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif
    try
    {
        BoxApp theApp(hInstance);
        if(!theApp.Initialize())
        {
            return 0;
        }
        return theApp.Run();
    }
    catch (DxException e)
    {
        MessageBox(nullptr,e.ToString().c_str(),L"HR Failed",MB_OK);
        return 0;
    }
    
}
