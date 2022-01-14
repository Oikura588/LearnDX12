#include <array>

#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "../Common/DDSTextureLoader.h"
#include <DirectXColors.h>
using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;


// 渲染层，分层管理不同绘制阶段的物体
enum class ERenderLayer :int
{
    Opaque = 0,
    Mirrors,
    Reflected,
    Transparent,
    Count
};

// FrameResources的数目.RenderItem和App都需要这个数据.
static const  int gNumFrameResources=1;
// 顶点信息
struct Vertex
{
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT2 TexC;
    Vertex()
    {

    }
    Vertex(float px, float py, float pz, float nx, float ny, float nz, float u, float v)
        :Pos(XMFLOAT3(px,py,pz)),Normal(XMFLOAT3(nx,ny,nz)),TexC(XMFLOAT2(u,v))
    {

    }
};
// 常量缓冲区.每个物体有自己的transform信息.
struct ObjectConstants
{
    XMFLOAT4X4 World = MathHelper::Identity4x4();
    XMFLOAT4X4 InvWorld = MathHelper::Identity4x4();
    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
};

struct MaterialConstants
{
    DirectX::XMFLOAT4 DiffuseAlbedo;
    DirectX::XMFLOAT3 FresnelR0;
    float Roughness;
    DirectX::XMFLOAT4X4 MaterialTransform;
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
    // 图元类型
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    // DrawInstance 的参数
    UINT IndexCount;
    UINT StartIndexLocation;
    int BaseVertexLocation;

    // 材质
    Material* Mat = nullptr;
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

    XMFLOAT4 AmbientLight;
    Light Lights[MaxLights];
};

// 以CPU每帧都需更新的资源作为基本元素，包括CmdListAlloc、ConstantBuffer等.
// Draw()中进行绘制时，执行CmdList的Reset函数，来指定当前FrameResource所使用的CmdAlloc,从而将绘制命令存储在每帧的Alloc中.
class FrameResource
{
public:
    FrameResource(ID3D12Device* device,UINT passCount,UINT objectCount,UINT MaterialCount)
    {
        device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(CmdAlloc.GetAddressOf())
        );
        ObjectsCB = std::make_unique<UploadBuffer<ObjectConstants>>(device,objectCount,true);
        PassCB = std::make_unique<UploadBuffer<PassConstants>>(device,passCount,true);
        MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device,MaterialCount,true);
    }
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;

    // GPU处理完与此Allocator相关的命令前，不能对其重置，所以每帧都需要保存自己的Alloc.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdAlloc;
    
    // 在GPU处理完此ConstantBuffer相关的命令前，不能对其重置
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectsCB = nullptr;
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
    std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr;

    // 每帧需要有自己的fence，来判断GPU与CPU的帧之间的同步.
    UINT64 Fence = 0;
};

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
    ComPtr<ID3D12DescriptorHeap> mCbvHeap       = nullptr;
	UINT mPassCbvOffset;
    UINT mMaterialCbvOffset;
	UINT mSrvOffset;

    // 存储所有渲染项.
    std::vector<std::unique_ptr<RenderItem>> mAllRenderItems;
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
    float mPhi = XM_PIDIV4;
    float mRadius = 5.0f;
    // 相机位置
    XMFLOAT3 mEyePos;

    POINT mLastMousePos;

    // 是否开启线框模式
    bool mIsWireframe = false;

    // 材质
    std::unordered_map<std::string,std::unique_ptr<Material>> mMaterials;

    // 纹理
    std::unordered_map<std::string,std::unique_ptr<Texture>> mTextures;

    // 采样器堆
    ComPtr<ID3D12DescriptorHeap> mSamplerHeap;

    // 不同渲染层
    std::vector<RenderItem*> mRenderItemsLayer[(int)ERenderLayer::Count];

    // 缓存镜像的skull模型
    XMFLOAT3 mSkullTranslation = {0.f,1.f,-5.0f};
	RenderItem* mSkullRenderItem = nullptr;
    RenderItem* mReflectedSkullRenderItem = nullptr;
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

    // 初始化纹理
    {
        auto brickTex = std::make_unique<Texture>();
        brickTex->Name = "bricksTex";
        brickTex->FileName = L"Textures\\bricks3.dds";
        ThrowIfFailed( DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),mCommandList.Get(),brickTex->FileName.c_str(),brickTex->Resource,brickTex->UploadHeap));

        auto checkboardTex = std::make_unique<Texture>();
        checkboardTex->Name = "checkboardTex";
        checkboardTex->FileName = L"Textures\\checkboard.dds";
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(), checkboardTex->FileName.c_str(), checkboardTex->Resource, checkboardTex->UploadHeap));

        auto iceTex = std::make_unique<Texture>();
        iceTex->Name = "iceTex";
        iceTex->FileName = L"Textures\\ice.dds";
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(), iceTex->FileName.c_str(), iceTex->Resource, iceTex->UploadHeap));

		auto white1x1Tex = std::make_unique<Texture>();
		white1x1Tex->Name = "white1x1Tex";
		white1x1Tex->FileName = L"Textures\\white1x1.dds";
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(), white1x1Tex->FileName.c_str(), white1x1Tex->Resource, white1x1Tex->UploadHeap));

        mTextures[brickTex->Name] = std::move(brickTex);
		mTextures[checkboardTex->Name] = std::move(checkboardTex);
		mTextures[iceTex->Name] = std::move(iceTex);
		mTextures[white1x1Tex->Name] = std::move(white1x1Tex);
    }

    // 初始化采样器堆
    {
        D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
        samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        samplerHeapDesc.NumDescriptors = 1;

        md3dDevice->CreateDescriptorHeap(
            &samplerHeapDesc,IID_PPV_ARGS(mSamplerHeap.GetAddressOf())
        );
    }
    // 初始化采样器
    {
        D3D12_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter  =D3D12_FILTER_MAXIMUM_MIN_LINEAR_MAG_MIP_POINT;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        // 仅限于3D纹理
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

        samplerDesc.MinLOD = 0;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;

        md3dDevice->CreateSampler(&samplerDesc,mSamplerHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // Build material.
    {
		// 创建材质
		auto brick = std::make_unique<Material>();
		brick->Name = "bricks";
		brick->MatCBIndex = 0;
		brick->NumFramesDirty = gNumFrameResources;
        brick->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        brick->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
        brick->Roughness = 0.25f;
        brick->DiffuseSrvHeapIndex = 0;           // texture 0
		brick->MatTransform = MathHelper::Identity4x4();          

		auto checkertile = std::make_unique<Material>();
		checkertile->Name = "checkertile";
		checkertile->MatCBIndex = 1;
        checkertile->NumFramesDirty = gNumFrameResources;
		checkertile->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		checkertile->FresnelR0 = XMFLOAT3(0.07f, 0.07f, 0.07f);
		checkertile->Roughness = 0.3f;
        checkertile->DiffuseSrvHeapIndex = 1;            // texture 1
        checkertile->MatTransform = MathHelper::Identity4x4();


		auto icemirror = std::make_unique<Material>();
		icemirror->Name = "icemirror";
		icemirror->MatCBIndex = 2;
        icemirror->NumFramesDirty = gNumFrameResources;
		icemirror->DiffuseAlbedo = XMFLOAT4(0.5f, 0.5f, 0.5f, 0.9f);
		icemirror->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
		icemirror->Roughness = 0.5f;
        icemirror->DiffuseSrvHeapIndex = 2;             // texture 2
        icemirror->MatTransform = MathHelper::Identity4x4();


		auto skullMat = std::make_unique<Material>();
		skullMat->Name = "skullMat";
		skullMat->MatCBIndex = 3;
        skullMat->DiffuseSrvHeapIndex = 3;
        skullMat->NumFramesDirty = gNumFrameResources;
		skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
		skullMat->Roughness = 0.3f;
        skullMat->MatTransform = MathHelper::Identity4x4();



		mMaterials["bricks"] = std::move(brick);
		mMaterials["checkertile"] = std::move(checkertile);
		mMaterials["icemirror"] = std::move(icemirror);
		mMaterials["skullMat"] = std::move(skullMat);
    }
	// Build Geometry.和渲染没什么关系了，就是创建buffer并保存起来，绘制的时候用
	{

        // 创建room geo
        {
			// Create and specify geometry.  For this sample we draw a floor
            // and a wall with a mirror on it.  We put the floor, wall, and
            // mirror geometry in one vertex buffer.
            //
            //   |--------------|
            //   |              |
            //   |----|----|----|
            //   |Wall|Mirr|Wall|
            //   |    | or |    |
            //   /--------------/
            //  /   Floor      /
            // /--------------/

		    std::array<Vertex, 20> vertices =
		    {
			    // Floor: Observe we tile texture coordinates.
			    Vertex(-3.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f), // 0 
			    Vertex(-3.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
			    Vertex(7.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f),
			    Vertex(7.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f),

			    // Wall: Observe we tile texture coordinates, and that we
			    // leave a gap in the middle for the mirror.
			    Vertex(-3.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 4
			    Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
			    Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f),
			    Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 2.0f),

			    Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 8 
			    Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
			    Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 0.0f),
			    Vertex(7.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 2.0f),

			    Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 12
			    Vertex(-3.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
			    Vertex(7.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 0.0f),
			    Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 1.0f),

			    // Mirror
			    Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 16
			    Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
			    Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f),
			    Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f)
		    };

		    std::array<std::int16_t, 30> indices =
		    {
			    // Floor
			    0, 1, 2,
			    0, 2, 3,

			    // Walls
			    4, 5, 6,
			    4, 6, 7,

			    8, 9, 10,
			    8, 10, 11,

			    12, 13, 14,
			    12, 14, 15,

			    // Mirror
			    16, 17, 18,
			    16, 18, 19
		    };

            SubmeshGeometry floorSubmesh;
            floorSubmesh.IndexCount = 6;
            floorSubmesh.StartIndexLocation = 0;
            floorSubmesh.BaseVertexLocation = 0;


            SubmeshGeometry wallSubmesh;
            wallSubmesh.IndexCount = 18;
            wallSubmesh.StartIndexLocation = 6;
            wallSubmesh.BaseVertexLocation = 0;

            SubmeshGeometry mirrorSubmesh;
            mirrorSubmesh.IndexCount = 6;
            mirrorSubmesh.StartIndexLocation =24;
            mirrorSubmesh.BaseVertexLocation = 0;

            UINT vbByteSize = (UINT)vertices.size()*sizeof(Vertex);
            UINT ibByteSize = (UINT)indices.size()*sizeof(std::uint16_t);

            auto geo = std::make_unique<MeshGeometry>();
            geo->Name = "roomGeo";
            geo->VertexBufferByteSize = vbByteSize;
            geo->VertexByteStride = sizeof(Vertex);
            geo->IndexFormat = DXGI_FORMAT_R16_UINT;
            geo->IndexBufferByteSize = ibByteSize;

            geo->DrawArgs["floor"] = floorSubmesh;
            geo->DrawArgs["wall"] = wallSubmesh;
            geo->DrawArgs["mirror"] = mirrorSubmesh;

            // 创建顶点buffer
            {
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
					IID_PPV_ARGS(geo->VertexBufferGPU.GetAddressOf())
				);
				// IB
				md3dDevice->CreateCommittedResource(
					&defaultBufferProperties,
					D3D12_HEAP_FLAG_NONE,
					&ibDesc,
					D3D12_RESOURCE_STATE_COMMON,
					nullptr,
					IID_PPV_ARGS(geo->IndexBufferGPU.GetAddressOf())
				);
				// 创建作为中介的上传缓冲区.
				defaultBufferProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

md3dDevice->CreateCommittedResource(
    &defaultBufferProperties,
    D3D12_HEAP_FLAG_NONE,
    &vbDesc,
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(geo->VertexBufferUploader.GetAddressOf())
);
md3dDevice->CreateCommittedResource(
    &defaultBufferProperties,
    D3D12_HEAP_FLAG_NONE,
    &ibDesc,
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(geo->IndexBufferUploader.GetAddressOf())
);


// 把默认的buffer改成写入状态来复制.
mCommandList->ResourceBarrier(
    1,
    &CD3DX12_RESOURCE_BARRIER::Transition(
        geo->VertexBufferGPU.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST
    )
);
mCommandList->ResourceBarrier(
    1,
    &CD3DX12_RESOURCE_BARRIER::Transition(
        geo->IndexBufferGPU.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST
    )
);

// 把数据拷贝到上传缓冲区.使用Subresource.
// 涉及到的调用:
// ID3D12Device::GetCopyableFootprints.
// UploaderBuffer::Map
// memcpy()?
// 思考，这里为什么不直接memcpy整个vertexbuffer，而是用subresource来处理.

BYTE* pData;

HRESULT hr = geo->VertexBufferUploader->Map(0, nullptr, reinterpret_cast<void**>(&pData));
ThrowIfFailed(hr);
UINT64 SrcOffset = 0;
UINT64 NumBytes = 0;

{
    memcpy(pData, vertices.data(), vbByteSize);
}
// Unmap.
geo->VertexBufferUploader->Unmap(0, nullptr);
pData = nullptr;
// copy buffer.
mCommandList->CopyBufferRegion(
    geo->VertexBufferGPU.Get(),
    0,
    geo->VertexBufferUploader.Get(),
    SrcOffset,
    vbByteSize
);

// 同样的流程，复制index buffer.
hr = geo->IndexBufferUploader->Map(0, nullptr, reinterpret_cast<void**>(&pData));
ThrowIfFailed(hr);
{
    memcpy(pData, indices.data(), ibByteSize);
}
geo->IndexBufferUploader->Unmap(0, nullptr);
pData = nullptr;
// copubuffer
mCommandList->CopyBufferRegion(
    geo->IndexBufferGPU.Get(),
    0,
    geo->IndexBufferUploader.Get(),
    0,
    ibByteSize

);


// Copy结束后，改变buffer状态
mCommandList->ResourceBarrier(
    1,
    &CD3DX12_RESOURCE_BARRIER::Transition(
        geo->VertexBufferGPU.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ
    )
);
mCommandList->ResourceBarrier(
    1,
    &CD3DX12_RESOURCE_BARRIER::Transition(
        geo->IndexBufferGPU.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ
    )
);
            }

            mGeometries[geo->Name] = std::move(geo);

        }

        // 加载模型
        {
		    GeometryGenerator::MeshData mesh = GeometryGenerator::LoadModel("Models\\skull.txt");
		    SubmeshGeometry meshSubmesh;
		    meshSubmesh.BaseVertexLocation = 0;
		    meshSubmesh.IndexCount = mesh.Indices32.size();
		    meshSubmesh.StartIndexLocation = 0;

		    std::vector<Vertex> vertices(mesh.Vertices.size());
		    for (size_t i = 0; i < mesh.Vertices.size(); ++i)
		    {
				vertices[i].Pos = mesh.Vertices[i].Position;
				vertices[i].Normal = mesh.Vertices[i].Normal;
				vertices[i].TexC = mesh.Vertices[i].TexC;
		    }

		    // 索引的缓冲区.
		    std::vector<std::uint16_t> indices;
		    indices.insert(indices.end(), std::begin(mesh.GetIndices16()), std::end(mesh.GetIndices16()));


		    // 创建几何体和子几何体，存储绘制所用到的Index、Vertex信息
		    auto meshGeo = std::make_unique<MeshGeometry>();
		    meshGeo->Name = "skullGeo";
		    UINT vbByteSize = vertices.size() * sizeof(Vertex);
		    UINT ibByteSize = indices.size() * sizeof(std::uint16_t);

		    meshGeo->VertexByteStride = sizeof(Vertex);
		    meshGeo->VertexBufferByteSize = vbByteSize;
		    meshGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
		    meshGeo->IndexBufferByteSize = ibByteSize;
		    meshGeo->DrawArgs["skull"] = meshSubmesh;


		    // 创建顶点缓冲区.
            {
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
					IID_PPV_ARGS(meshGeo->VertexBufferGPU.GetAddressOf())
				);
				// IB
				md3dDevice->CreateCommittedResource(
					&defaultBufferProperties,
					D3D12_HEAP_FLAG_NONE,
					&ibDesc,
					D3D12_RESOURCE_STATE_COMMON,
					nullptr,
					IID_PPV_ARGS(meshGeo->IndexBufferGPU.GetAddressOf())
				);
				// 创建作为中介的上传缓冲区.
				defaultBufferProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

				md3dDevice->CreateCommittedResource(
					&defaultBufferProperties,
					D3D12_HEAP_FLAG_NONE,
					&vbDesc,
					D3D12_RESOURCE_STATE_GENERIC_READ,
					nullptr,
					IID_PPV_ARGS(meshGeo->VertexBufferUploader.GetAddressOf())
				);
				md3dDevice->CreateCommittedResource(
					&defaultBufferProperties,
					D3D12_HEAP_FLAG_NONE,
					&ibDesc,
					D3D12_RESOURCE_STATE_GENERIC_READ,
					nullptr,
					IID_PPV_ARGS(meshGeo->IndexBufferUploader.GetAddressOf())
				);


				// 把默认的buffer改成写入状态来复制.
				mCommandList->ResourceBarrier(
					1,
					&CD3DX12_RESOURCE_BARRIER::Transition(
                        meshGeo->VertexBufferGPU.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST
					)
				);
				mCommandList->ResourceBarrier(
					1,
					&CD3DX12_RESOURCE_BARRIER::Transition(
                        meshGeo->IndexBufferGPU.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST
					)
				);



				// 把数据拷贝到上传缓冲区.使用Subresource.
				// 涉及到的调用:
				// ID3D12Device::GetCopyableFootprints.
				// UploaderBuffer::Map
				// memcpy()?
				// 思考，这里为什么不直接memcpy整个vertexbuffer，而是用subresource来处理.

				BYTE* pData;

				HRESULT hr = meshGeo->VertexBufferUploader->Map(0, nullptr, reinterpret_cast<void**>(&pData));
				ThrowIfFailed(hr);
				UINT64 SrcOffset = 0;
				UINT64 NumBytes = 0;

				{
					memcpy(pData, vertices.data(), vbByteSize);
				}
				// Unmap.
                meshGeo->VertexBufferUploader->Unmap(0, nullptr);
				pData = nullptr;
				// copy buffer.
				mCommandList->CopyBufferRegion(
                    meshGeo->VertexBufferGPU.Get(),
					0,
                    meshGeo->VertexBufferUploader.Get(),
					SrcOffset,
					vbByteSize
				);

				// 同样的流程，复制index buffer.
				hr = meshGeo->IndexBufferUploader->Map(0, nullptr, reinterpret_cast<void**>(&pData));
				ThrowIfFailed(hr);
				{
					memcpy(pData, indices.data(), ibByteSize);
				}
                meshGeo->IndexBufferUploader->Unmap(0, nullptr);
				pData = nullptr;
				// copubuffer
				mCommandList->CopyBufferRegion(
                    meshGeo->IndexBufferGPU.Get(),
					0,
                    meshGeo->IndexBufferUploader.Get(),
					0,
					ibByteSize

				);


				// Copy结束后，改变buffer状态
				mCommandList->ResourceBarrier(
					1,
					&CD3DX12_RESOURCE_BARRIER::Transition(
                        meshGeo->VertexBufferGPU.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ
					)
				);
				mCommandList->ResourceBarrier(
					1,
					&CD3DX12_RESOURCE_BARRIER::Transition(
                        meshGeo->IndexBufferGPU.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ
					)
				);
            }
		    

		    mGeometries[meshGeo->Name] = std::move(meshGeo);
        }
	}

	// 构建几何体后就可以Build渲染项了，渲染项可以理解为是几何体的实例化，每个渲染项是场景中的一个物体，比如可能有多个圆台形组成的物体
	{
        auto floorRenderItem = std::make_unique<RenderItem>();
	    floorRenderItem->World = MathHelper::Identity4x4();
	    floorRenderItem->ObjCBOffset = 0;
	    floorRenderItem->Geo = mGeometries["roomGeo"].get();
	    floorRenderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	    floorRenderItem->BaseVertexLocation = floorRenderItem->Geo->DrawArgs["floor"].BaseVertexLocation;
	    floorRenderItem->StartIndexLocation = floorRenderItem->Geo->DrawArgs["floor"].StartIndexLocation;
	    floorRenderItem->IndexCount = floorRenderItem->Geo->DrawArgs["floor"].IndexCount;
	    floorRenderItem->Mat = mMaterials["checkertile"].get();
	    mRenderItemsLayer[(int)ERenderLayer::Opaque].push_back(floorRenderItem.get());


		auto wallsRenderItem = std::make_unique<RenderItem>();
		wallsRenderItem->ObjCBOffset = 1;
		wallsRenderItem->Geo = mGeometries["roomGeo"].get();
		wallsRenderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wallsRenderItem->BaseVertexLocation = wallsRenderItem->Geo->DrawArgs["wall"].BaseVertexLocation;
		wallsRenderItem->StartIndexLocation = wallsRenderItem->Geo->DrawArgs["wall"].StartIndexLocation;
		wallsRenderItem->IndexCount = wallsRenderItem->Geo->DrawArgs["wall"].IndexCount;
        wallsRenderItem->Mat = mMaterials["bricks"].get();
		mRenderItemsLayer[(int)ERenderLayer::Opaque].push_back(wallsRenderItem.get());


		auto skullRenderItem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&skullRenderItem->World,XMMatrixRotationY(90.0F)*XMMatrixScaling(0.3,0.3,0.3));
		skullRenderItem->ObjCBOffset = 2;
		skullRenderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		skullRenderItem->Geo = mGeometries["skullGeo"].get();
		skullRenderItem->BaseVertexLocation = skullRenderItem->Geo->DrawArgs["skull"].BaseVertexLocation;
		skullRenderItem->StartIndexLocation = skullRenderItem->Geo->DrawArgs["skull"].StartIndexLocation;
		skullRenderItem->IndexCount = skullRenderItem->Geo->DrawArgs["skull"].IndexCount;
        skullRenderItem->Mat = mMaterials["skullMat"].get();
        mSkullRenderItem = skullRenderItem.get();
		mRenderItemsLayer[(int)ERenderLayer::Opaque].push_back(skullRenderItem.get());


        // 反射后的物体信息
        auto reflectedSkullRenderItem = std::make_unique<RenderItem>();
        *reflectedSkullRenderItem = *skullRenderItem;
        reflectedSkullRenderItem->ObjCBOffset = 3;
        mReflectedSkullRenderItem = reflectedSkullRenderItem.get();
        mRenderItemsLayer[(int)ERenderLayer::Reflected].push_back(reflectedSkullRenderItem.get());


        // 镜面信息，会绘制两次，第一次绘制到模板缓冲区，第二次绘制出镜像.
        auto mirrorRenderItem = std::make_unique<RenderItem>();
        mirrorRenderItem->World = MathHelper::Identity4x4();
        mirrorRenderItem->ObjCBOffset = 4;
        mirrorRenderItem->Mat = mMaterials["icemirror"].get();
        mirrorRenderItem->Geo = mGeometries["roomGeo"].get();
        mirrorRenderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        mirrorRenderItem->IndexCount = mirrorRenderItem->Geo->DrawArgs["mirror"].IndexCount;
		mirrorRenderItem->BaseVertexLocation = mirrorRenderItem->Geo->DrawArgs["mirror"].BaseVertexLocation;
		mirrorRenderItem->StartIndexLocation = mirrorRenderItem->Geo->DrawArgs["mirror"].StartIndexLocation;
        mRenderItemsLayer[(int)ERenderLayer::Mirrors].push_back(mirrorRenderItem.get());
        mRenderItemsLayer[(int)ERenderLayer::Transparent].push_back(mirrorRenderItem.get());




	
		mAllRenderItems.push_back(std::move(floorRenderItem));
        mAllRenderItems.push_back(std::move(wallsRenderItem));
        mAllRenderItems.push_back(std::move(skullRenderItem));
        mAllRenderItems.push_back(std::move(reflectedSkullRenderItem));
        mAllRenderItems.push_back(std::move(mirrorRenderItem));
	} 
 
    // 初始化多个帧资源.需要给按照物体个数初始化常量缓冲区，所以需要事先知道物体个数.
    {
        for(int i=0;i<gNumFrameResources;++i)
        {
            mFrameResources.push_back(
                std::make_unique<FrameResource>(md3dDevice.Get(),2,mAllRenderItems.size(),mMaterials.size())
            );
        }
    }
	// 创建常量缓冲区堆
	{
		UINT objCount = (UINT)mAllRenderItems.size();
        UINT matCount = (UINT)mMaterials.size();
        UINT textureCount = (UINT)mTextures.size();
        mMaterialCbvOffset = objCount*gNumFrameResources;
		mPassCbvOffset = mMaterialCbvOffset + matCount*gNumFrameResources;
        mSrvOffset = mPassCbvOffset + 1*gNumFrameResources;
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		// 描述符个数等于物体数量*Frame数量，每个Frame还有Pass和材质数据，所以额外加上
        // 多一个额外绘制镜面内Pass灯光的常量缓冲区.
		heapDesc.NumDescriptors = (objCount + 2 +matCount + textureCount) * gNumFrameResources;
		// Shader可见，因为需要读取
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heapDesc.NodeMask = 0;

		ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
			&heapDesc,
			IID_PPV_ARGS(mCbvHeap.GetAddressOf())
		));
	}
    
    // 初始化常量缓冲区View.
    {
       // 由于有多个FrameResource,每个FrameResource内、每个物体的有自己的ConstantBufferView。
        // 所有的FrameResource内的物体偏移是相同的，所以偏移信息存在物体中。

		// 创建view需要找到GPU的缓冲区地址，以及CPU的描述符地址
		UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
		UINT objCount = (UINT)(mAllRenderItems.size());

		for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
		{
			auto objectCB = mFrameResources[frameIndex]->ObjectsCB->Resource();

			for (UINT i = 0; i < objCount; ++i)
			{
				// 第i个物体的缓冲区地址.
				D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();
				cbAddress += i* objCBByteSize;

				D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
				cbvDesc.SizeInBytes = objCBByteSize;
				cbvDesc.BufferLocation = cbAddress;

				// 描述符地址
				D3D12_CPU_DESCRIPTOR_HANDLE cbHandle = mCbvHeap->GetCPUDescriptorHandleForHeapStart();
				cbHandle.ptr+=(frameIndex*objCount + i)* mCbvUavDescriptorSize;
				
				// 第i个物体的描述符地址.
				md3dDevice->CreateConstantBufferView(
					&cbvDesc,cbHandle
				);
				//
			}
		}

        // Material constant
        for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
        {
            auto materialCB = mFrameResources[frameIndex]->MaterialCB->Resource();
			UINT materialCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
            UINT materialCount = mMaterials.size();
            for (UINT i = 0; i < materialCount; ++i)
            {
                D3D12_GPU_VIRTUAL_ADDRESS cbAddress = materialCB->GetGPUVirtualAddress();
                cbAddress += i*materialCBByteSize;

                D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
                cbvDesc.BufferLocation = cbAddress;
                cbvDesc.SizeInBytes = materialCBByteSize;

                D3D12_CPU_DESCRIPTOR_HANDLE cbHandle = mCbvHeap->GetCPUDescriptorHandleForHeapStart();
                cbHandle.ptr += (mMaterialCbvOffset+frameIndex*materialCount+i)* mCbvUavDescriptorSize;
				md3dDevice->CreateConstantBufferView(&cbvDesc, cbHandle);
            }
        }

		// Pass constant.
		for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
		{
			auto PassCB = mFrameResources[frameIndex]->PassCB->Resource();
			UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
			D3D12_GPU_VIRTUAL_ADDRESS passAddress = PassCB->GetGPUVirtualAddress();

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = passAddress;
			cbvDesc.SizeInBytes = passCBByteSize;

			D3D12_CPU_DESCRIPTOR_HANDLE cbHandle = mCbvHeap->GetCPUDescriptorHandleForHeapStart();
			cbHandle.ptr+=(mPassCbvOffset+frameIndex)*mCbvUavDescriptorSize;

			md3dDevice->CreateConstantBufferView(&cbvDesc,cbHandle);

            passAddress = PassCB->GetGPUVirtualAddress()+passCBByteSize;
            cbvDesc.BufferLocation = passAddress;
            // 额外创建一个
            cbHandle.ptr+=mCbvUavDescriptorSize;
			md3dDevice->CreateConstantBufferView(&cbvDesc, cbHandle);

		}

        // 纹理资源
        for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE cbHandle = mCbvHeap->GetCPUDescriptorHandleForHeapStart();
            cbHandle.ptr +=(mSrvOffset+frameIndex*mTextures.size())*mCbvUavDescriptorSize;
            D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceDesc={};
            shaderResourceDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            shaderResourceDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            shaderResourceDesc.Texture2D.MostDetailedMip = 0;
            shaderResourceDesc.Texture2D.ResourceMinLODClamp = 0.0f;

            for (auto& e : mTextures)
            {
                auto texResource = e.second->Resource;
                shaderResourceDesc.Format = texResource->GetDesc().Format;
                shaderResourceDesc.Texture2D.MipLevels = texResource->GetDesc().MipLevels;
                md3dDevice->CreateShaderResourceView(texResource.Get(),&shaderResourceDesc,cbHandle);
				cbHandle.ptr += mCbvUavDescriptorSize;
            }


   //         auto texResource = mTextures["bricksTex"]->Resource;
			//auto stoneResource = mTextures["stoneTex"]->Resource;
			//auto tileResource = mTextures["tileTex"]->Resource;

   //         shaderResourceDesc.Format = texResource->GetDesc().Format;
   //         shaderResourceDesc.Texture2D.MipLevels = texResource->GetDesc().MipLevels;
   //         md3dDevice->CreateShaderResourceView(
   //             texResource.Get(),&shaderResourceDesc, cbHandle
   //         );

   //         // 第二个纹理
   //         cbHandle.ptr+=mCbvUavDescriptorSize;
   //         shaderResourceDesc.Format = stoneResource->GetDesc().Format;
   //         shaderResourceDesc.Texture2D.MipLevels = stoneResource->GetDesc().MipLevels;
			//md3dDevice->CreateShaderResourceView(
			//	stoneResource.Get(), &shaderResourceDesc, cbHandle
			//);

   //         // 第三个纹理
			// // 第二个纹理
			//cbHandle.ptr += mCbvUavDescriptorSize;
			//shaderResourceDesc.Format = tileResource->GetDesc().Format;
			//shaderResourceDesc.Texture2D.MipLevels = tileResource->GetDesc().MipLevels;
			//md3dDevice->CreateShaderResourceView(
   //             tileResource.Get(), &shaderResourceDesc, cbHandle
			//);
           
        }
    }
    
    // 初始化RootSignature，把常量缓冲区绑定到GPU上供Shader读取.
    // 把着色器当作一个函数，root signature就是函数签名，资源是参数数据.
    {
        // root signature -> root parameter -> type/table -> range.
        // Object
        D3D12_DESCRIPTOR_RANGE descRange;
        descRange.NumDescriptors = 1;;
        descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        // 思考，这里是指r(0)吗？
        // 这里必须知名base shader register，否则会导致和shader不匹配.
        descRange.BaseShaderRegister = 0;
        descRange.RegisterSpace = 0;
        descRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        
        
        D3D12_ROOT_DESCRIPTOR_TABLE  rootDescTable;
        rootDescTable.pDescriptorRanges = &descRange;
        rootDescTable.NumDescriptorRanges = 1;
        // Material
        D3D12_DESCRIPTOR_RANGE materialRange;
        materialRange.NumDescriptors = 1;
        materialRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        materialRange.RegisterSpace = 0;
        materialRange.BaseShaderRegister = 1;
        materialRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_DESCRIPTOR_TABLE materialDescTable;
        materialDescTable.pDescriptorRanges = & materialRange;
        materialDescTable.NumDescriptorRanges = 1;

        // Pass
        D3D12_DESCRIPTOR_RANGE passDescRange;
        passDescRange.NumDescriptors = 1;
        passDescRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        // 思考,这里RegisterSpace意义还不清楚，下面那个对应着色器中的b0、b1之类的.
        passDescRange.RegisterSpace = 0;
        passDescRange.BaseShaderRegister=2;
        passDescRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_DESCRIPTOR_TABLE passDescTable;
        passDescTable.pDescriptorRanges =&passDescRange;
        passDescTable.NumDescriptorRanges = 1;

        // SRV
		D3D12_DESCRIPTOR_RANGE srvDescRange;
		srvDescRange.NumDescriptors = 1;
		srvDescRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		srvDescRange.RegisterSpace = 0;
		srvDescRange.BaseShaderRegister = 0;
		srvDescRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_DESCRIPTOR_TABLE srvDescTable;
		srvDescTable.pDescriptorRanges = &srvDescRange;
		srvDescTable.NumDescriptorRanges = 1;

        // Sampler
		D3D12_DESCRIPTOR_RANGE samplerDescRange;
		samplerDescRange.NumDescriptors = 1;
		samplerDescRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		samplerDescRange.RegisterSpace = 0;
		samplerDescRange.BaseShaderRegister = 0;
		samplerDescRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_DESCRIPTOR_TABLE samplerDescTable;
		samplerDescTable.pDescriptorRanges = &samplerDescRange;
		samplerDescTable.NumDescriptorRanges = 1;


        

        
        D3D12_ROOT_PARAMETER slotRootParameter[5];
        // object 
        slotRootParameter[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        slotRootParameter[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        slotRootParameter[0].DescriptorTable = rootDescTable;

        // material
        slotRootParameter[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        slotRootParameter[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        slotRootParameter[1].DescriptorTable = materialDescTable;

        // pass
        slotRootParameter[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        slotRootParameter[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        slotRootParameter[2].DescriptorTable = passDescTable;

        // srv
        slotRootParameter[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        slotRootParameter[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        slotRootParameter[3].DescriptorTable = srvDescTable;

        // Sampler
        slotRootParameter[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        slotRootParameter[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        slotRootParameter[4].DescriptorTable = samplerDescTable;
        
        
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
        mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
        // 使用描述符表，要设置描述符堆以及
        ID3D12DescriptorHeap* descriptorHeap[] = {mCbvHeap.Get()};
        mCommandList->SetDescriptorHeaps(_countof(descriptorHeap),descriptorHeap);
        // 偏移到此次绘制调用的CBV处(因为用一个大的堆来存储所有物体的常量缓冲区.)
        D3D12_GPU_DESCRIPTOR_HANDLE cbv = mCbvHeap->GetGPUDescriptorHandleForHeapStart();
        
        mCommandList->SetGraphicsRootDescriptorTable(
            0,
            cbv
        );
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
            &pipelineStateDesc,IID_PPV_ARGS(&mPSOs["defaultPSO"])
        ));

        // 创建线框模式pso.

        rasterizerDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;
        pipelineStateDesc.RasterizerState = rasterizerDesc;
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
			&pipelineStateDesc, IID_PPV_ARGS(&mPSOs["wireframePSO"])
		));

        // 镜面模板缓冲区的pso，这里是指镜面渲染到模板缓冲区的过程

        D3D12_DEPTH_STENCIL_DESC mirrorDesc ={};
        mirrorDesc.DepthEnable = true;
        mirrorDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        mirrorDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        mirrorDesc.StencilEnable = true;
        mirrorDesc.StencilReadMask = 0xff;
        mirrorDesc.StencilWriteMask = 0xff;

        mirrorDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        mirrorDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        mirrorDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
        mirrorDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

		mirrorDesc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		mirrorDesc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		mirrorDesc.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
		mirrorDesc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

        // 不渲染背面，所以随意
		rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
		pipelineStateDesc.RasterizerState = rasterizerDesc;
        pipelineStateDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
        pipelineStateDesc.DepthStencilState = mirrorDesc;
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
			&pipelineStateDesc, IID_PPV_ARGS(&mPSOs["markStencilMirrors"])
		));

        // 绘制镜面中模型的PSO，这里是指绘制到后台缓冲区的过程.

        D3D12_DEPTH_STENCIL_DESC reflectionDesc={};
        reflectionDesc.DepthEnable = true;
        reflectionDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        reflectionDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        reflectionDesc.StencilEnable = true;
        reflectionDesc.StencilReadMask = 0xff;
        reflectionDesc.StencilWriteMask = 0XFF;

        reflectionDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        reflectionDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        reflectionDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        reflectionDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

		reflectionDesc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		reflectionDesc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		reflectionDesc.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		reflectionDesc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

        pipelineStateDesc.BlendState.RenderTarget[0] = rtDesc;
        pipelineStateDesc.DepthStencilState = reflectionDesc;
        pipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        // 镜面中的法线是反的，要设置绕序
        pipelineStateDesc.RasterizerState.FrontCounterClockwise = true;     
        
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
			&pipelineStateDesc, IID_PPV_ARGS(&mPSOs["drawStencilReflections"])
		));


        // 绘制镜面本身的透明Pass.
        D3D12_RENDER_TARGET_BLEND_DESC mirrorBlendDesc={};
        mirrorBlendDesc.BlendEnable = true;
        mirrorBlendDesc.LogicOpEnable = false;
        mirrorBlendDesc.SrcBlend = D3D12_BLEND_ZERO;
        mirrorBlendDesc.DestBlend = D3D12_BLEND_SRC_COLOR;
        mirrorBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
        mirrorBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
        mirrorBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
        mirrorBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        mirrorBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
        mirrorBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        pipelineStateDesc.BlendState.RenderTarget[0] = mirrorBlendDesc;
        pipelineStateDesc.DepthStencilState = dsDesc;
        pipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        pipelineStateDesc.RasterizerState.FrontCounterClockwise = false;

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
    // 更新镜像的模型位置
    {
		const float dt = gt.DeltaTime();

		if (GetAsyncKeyState('A') & 0x8000)
			mSkullTranslation.x -= 1.0f * dt;

		if (GetAsyncKeyState('D') & 0x8000)
			mSkullTranslation.x += 1.0f * dt;

		if (GetAsyncKeyState('W') & 0x8000)
			mSkullTranslation.y += 1.0f * dt;

		if (GetAsyncKeyState('S') & 0x8000)
			mSkullTranslation.y -= 1.0f * dt;

		// Don't let user move below ground plane.
		mSkullTranslation.y = mSkullTranslation.y > 0.0f?mSkullTranslation.y:0.0f;

		// Update the new world matrix.
		XMMATRIX skullRotate = XMMatrixRotationY(0.5f * MathHelper::Pi);
		XMMATRIX skullScale = XMMatrixScaling(0.45f, 0.45f, 0.45f);
		XMMATRIX skullOffset = XMMatrixTranslation(mSkullTranslation.x, mSkullTranslation.y, mSkullTranslation.z);
		XMMATRIX skullWorld = skullRotate * skullScale * skullOffset;
		XMStoreFloat4x4(&mSkullRenderItem->World, skullWorld);
        mSkullRenderItem->NumFramesDirty = gNumFrameResources;

        XMVECTOR mirrorPlane = XMVectorSet(0.f,0.f,1.f,0.f); // xy plane
        XMMATRIX R = XMMatrixReflect(mirrorPlane);
        XMStoreFloat4x4(&mReflectedSkullRenderItem->World,skullWorld*R);
        mReflectedSkullRenderItem->NumFramesDirty = gNumFrameResources;

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
                XMVECTOR det = XMMatrixDeterminant(world);
				XMStoreFloat4x4(&objConstants.InvWorld, XMMatrixTranspose(XMMatrixInverse(&det,world)));
                currObjectCB->CopyData(e->ObjCBOffset,objConstants);
                e->NumFramesDirty--;
            }
            
        }
    }

    // 更新Material的CB
    {
        auto currMaterialCB = mCurrentFrameResource->MaterialCB.get();
        for (auto& e : mMaterials)
        {
            Material* mat = e.second.get();
            if (mat->NumFramesDirty > 0)
            {
				MaterialConstants matConstants;
				matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
				matConstants.FresnelR0 = mat->FresnelR0;
				matConstants.MaterialTransform = mat->MatTransform;
				matConstants.Roughness = mat->Roughness;

				currMaterialCB->CopyData(mat->MatCBIndex, matConstants);
				mat->NumFramesDirty--;
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

        // 环境光
        passConstants.AmbientLight = XMFLOAT4(0.25f,0.25f,0.35f,1.0f);
        // 三个光源
		passConstants.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
		passConstants.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
		passConstants.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
		passConstants.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
		passConstants.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
		passConstants.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };
	
        currPassCB->CopyData(0,passConstants);

        // 更新第二个Pass
		PassConstants reflcetPassConstants = passConstants;

        // 计算镜面中的灯光位置等.
        XMVECTOR mirrorPlane = XMVectorSet(0.0f,0.0f,1.0f,0.0f);
        XMMATRIX R = XMMatrixReflect(mirrorPlane);

        for (int i = 0; i < 3; ++i)
        {
            XMVECTOR lightDir = XMLoadFloat3(&passConstants.Lights[i].Direction);
            XMVECTOR reflcetedLightDir = XMVector3TransformNormal(lightDir,R);
            XMStoreFloat3(&reflcetPassConstants.Lights[i].Direction,reflcetedLightDir);
        }

        auto PassCB = mCurrentFrameResource->PassCB.get();
        currPassCB->CopyData(1,reflcetPassConstants);
    }
    
    // // 更新Constant Buffer.
    // float x = mRadius*sinf(mPhi)*cosf(mTheta);
    // float y = mRadius*sinf(mPhi)*sinf(mTheta);
    // float z = mRadius*cosf(mPhi);
    //
    // // View matrix.
    // XMVECTOR pos = XMVectorSet(x,y,z,1.0f);
    // XMVECTOR target = XMVectorZero();
    // XMVECTOR up = XMVectorSet(0.f,1,0.f,0.f);
    //
    // XMMATRIX view = XMMatrixLookAtLH(pos,target,up);
    // XMStoreFloat4x4(&mView,view);
    //
    // XMMATRIX model = XMLoadFloat4x4(&mWorld);
    // XMMATRIX proj = XMLoadFloat4x4(&mProj);
    // XMMATRIX mvp = model*view*proj;
    //
    // // 更新常量缓冲区.
    // ObjectConstants objectConstants;
    // // hlsl是列主序矩阵，DXMath中的矩阵传递时需要转置
    // XMStoreFloat4x4(&objectConstants.ModelViewProj,XMMatrixTranspose( mvp));
    // mObjectCB->CopyData(0,objectConstants);
}

void BoxApp::Draw(const GameTimer& gt)
{
    //FlushCommandQueue();
    // cmd相关Reset
    mDirectCmdListAlloc->Reset();   // cmdlist 执行完后才能重置,即FlushCommandQuene之后.
    if (mIsWireframe)
    {
        mCommandList->Reset(mDirectCmdListAlloc.Get(),mPSOs["wireframePSO"].Get());
    }
    else
    { 
        mCommandList->Reset(mDirectCmdListAlloc.Get(),mPSOs["defaultPSO"].Get());  // 传入Queue后就可以重置.
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


    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	// 设置采样器
	// 描述符相关.用来更新采样器
	ID3D12DescriptorHeap* samplerHeaps[] = { mSamplerHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(samplerHeaps), samplerHeaps);
	mCommandList->SetGraphicsRootDescriptorTable(4, mSamplerHeap->GetGPUDescriptorHandleForHeapStart());

	// 描述符相关.用来更新常量缓冲区
	ID3D12DescriptorHeap* descHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descHeaps), descHeaps);

    int passCbvIndex = mPassCbvOffset + mCurrentFrameIndex;
    auto passCbvHandle = mCbvHeap->GetGPUDescriptorHandleForHeapStart();
    passCbvHandle.ptr +=passCbvIndex*mCbvUavDescriptorSize;
    mCommandList->SetGraphicsRootDescriptorTable(2,passCbvHandle);

    // 绘制一个物体需要绑定两个buffer、设置图元类型、设置常量缓冲区等，把这些绘制一个物体需要的数据整合起来，可以作为RenderItem.
	// 绘制物体
	{
		UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
		UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

		auto objCB = mCurrentFrameResource->ObjectsCB->Resource();

        // 绘制不透明层
		for (size_t i = 0; i < mRenderItemsLayer[(int)ERenderLayer::Opaque].size(); ++i)
		{
			auto ri = mRenderItemsLayer[(int)ERenderLayer::Opaque][i];
			mCommandList->IASetVertexBuffers(0,1,&ri->Geo->VertexBufferView());
			mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
			mCommandList->IASetPrimitiveTopology(ri->PrimitiveTopology);
			D3D12_GPU_DESCRIPTOR_HANDLE cbvHandle =  mCbvHeap->GetGPUDescriptorHandleForHeapStart();
			cbvHandle.ptr+= (ri->ObjCBOffset+ mAllRenderItems.size()*mCurrentFrameIndex)*mCbvUavDescriptorSize;
			mCommandList->SetGraphicsRootDescriptorTable(0, cbvHandle);
            // 设置材质
            D3D12_GPU_DESCRIPTOR_HANDLE matHandle= mCbvHeap->GetGPUDescriptorHandleForHeapStart();
            matHandle.ptr += (mMaterialCbvOffset + mCurrentFrameIndex * mMaterials.size() + ri->Mat->MatCBIndex) * mCbvUavDescriptorSize;
			mCommandList->SetGraphicsRootDescriptorTable(1, matHandle);

            // 设置纹理
            D3D12_GPU_DESCRIPTOR_HANDLE texHandle = mCbvHeap->GetGPUDescriptorHandleForHeapStart();
            texHandle.ptr +=(mSrvOffset + mCurrentFrameIndex*mTextures.size() + ri->Mat->DiffuseSrvHeapIndex) *mCbvUavDescriptorSize;
            mCommandList->SetGraphicsRootDescriptorTable(3,texHandle);


			mCommandList->DrawIndexedInstanced(ri->IndexCount,1,ri->StartIndexLocation,ri->BaseVertexLocation,0);
            //mCommandList->DrawIndexedInstanced(3,1,0,0,0);
		}

        // 绘制镜子的模板
		mCommandList->OMSetStencilRef(5);
        mCommandList->SetPipelineState(mPSOs["markStencilMirrors"].Get());
		// 绘制镜像层
		for (size_t i = 0; i < mRenderItemsLayer[(int)ERenderLayer::Mirrors].size(); ++i)
		{
			auto ri = mRenderItemsLayer[(int)ERenderLayer::Mirrors][i];
			mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
			mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
			mCommandList->IASetPrimitiveTopology(ri->PrimitiveTopology);
			D3D12_GPU_DESCRIPTOR_HANDLE cbvHandle = mCbvHeap->GetGPUDescriptorHandleForHeapStart();
			cbvHandle.ptr += (ri->ObjCBOffset + mAllRenderItems.size() * mCurrentFrameIndex) * mCbvUavDescriptorSize;
			mCommandList->SetGraphicsRootDescriptorTable(0, cbvHandle);
			// 设置材质
			D3D12_GPU_DESCRIPTOR_HANDLE matHandle = mCbvHeap->GetGPUDescriptorHandleForHeapStart();
			matHandle.ptr += (mMaterialCbvOffset + mCurrentFrameIndex * mMaterials.size() + ri->Mat->MatCBIndex) * mCbvUavDescriptorSize;
			mCommandList->SetGraphicsRootDescriptorTable(1, matHandle);

			// 设置纹理
			D3D12_GPU_DESCRIPTOR_HANDLE texHandle = mCbvHeap->GetGPUDescriptorHandleForHeapStart();
			texHandle.ptr += (mSrvOffset + mCurrentFrameIndex * mTextures.size() + ri->Mat->DiffuseSrvHeapIndex) * mCbvUavDescriptorSize;
			mCommandList->SetGraphicsRootDescriptorTable(3, texHandle);
			mCommandList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
			//mCommandList->DrawIndexedInstanced(3,1,0,0,0);
		}




        // 绘制反射层
		mCommandList->SetPipelineState(mPSOs["drawStencilReflections"].Get());

		for (size_t i = 0; i < mRenderItemsLayer[(int)ERenderLayer::Reflected].size(); ++i)
		{
			auto ri = mRenderItemsLayer[(int)ERenderLayer::Reflected][i];
			mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
			mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
			mCommandList->IASetPrimitiveTopology(ri->PrimitiveTopology);
			D3D12_GPU_DESCRIPTOR_HANDLE cbvHandle = mCbvHeap->GetGPUDescriptorHandleForHeapStart();
			cbvHandle.ptr += (ri->ObjCBOffset + mAllRenderItems.size() * mCurrentFrameIndex) * mCbvUavDescriptorSize;
			mCommandList->SetGraphicsRootDescriptorTable(0, cbvHandle);
			// 设置材质
			D3D12_GPU_DESCRIPTOR_HANDLE matHandle = mCbvHeap->GetGPUDescriptorHandleForHeapStart();
			matHandle.ptr += (mMaterialCbvOffset + mCurrentFrameIndex * mMaterials.size() + ri->Mat->MatCBIndex) * mCbvUavDescriptorSize;
			mCommandList->SetGraphicsRootDescriptorTable(1, matHandle);

			// 设置纹理
			D3D12_GPU_DESCRIPTOR_HANDLE texHandle = mCbvHeap->GetGPUDescriptorHandleForHeapStart();
			texHandle.ptr += (mSrvOffset + mCurrentFrameIndex * mTextures.size() + ri->Mat->DiffuseSrvHeapIndex) * mCbvUavDescriptorSize;
			mCommandList->SetGraphicsRootDescriptorTable(3, texHandle);
			mCommandList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
			//mCommandList->DrawIndexedInstanced(3,1,0,0,0);
		}
        // 反射层绘制结束重置模板值
		mCommandList->OMSetStencilRef(0);

        // 绘制镜面透明层
		mCommandList->SetPipelineState(mPSOs["transparentPSO"].Get());

		for (size_t i = 0; i < mRenderItemsLayer[(int)ERenderLayer::Transparent].size(); ++i)
		{
			auto ri = mRenderItemsLayer[(int)ERenderLayer::Transparent][i];
			mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
			mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
			mCommandList->IASetPrimitiveTopology(ri->PrimitiveTopology);
			D3D12_GPU_DESCRIPTOR_HANDLE cbvHandle = mCbvHeap->GetGPUDescriptorHandleForHeapStart();
			cbvHandle.ptr += (ri->ObjCBOffset + mAllRenderItems.size() * mCurrentFrameIndex) * mCbvUavDescriptorSize;
			mCommandList->SetGraphicsRootDescriptorTable(0, cbvHandle);
			// 设置材质
			D3D12_GPU_DESCRIPTOR_HANDLE matHandle = mCbvHeap->GetGPUDescriptorHandleForHeapStart();
			matHandle.ptr += (mMaterialCbvOffset + mCurrentFrameIndex * mMaterials.size() + ri->Mat->MatCBIndex) * mCbvUavDescriptorSize;
			mCommandList->SetGraphicsRootDescriptorTable(1, matHandle);

			// 设置纹理
			D3D12_GPU_DESCRIPTOR_HANDLE texHandle = mCbvHeap->GetGPUDescriptorHandleForHeapStart();
			texHandle.ptr += (mSrvOffset + mCurrentFrameIndex * mTextures.size() + ri->Mat->DiffuseSrvHeapIndex) * mCbvUavDescriptorSize;
			mCommandList->SetGraphicsRootDescriptorTable(3, texHandle);
			mCommandList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
			//mCommandList->DrawIndexedInstanced(3,1,0,0,0);
		}

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
    if((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.005 unit in the scene.
        float dx = 0.005f*static_cast<float>(x - mLastMousePos.x);
        float dy = 0.005f*static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 3.0f, 15.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
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
