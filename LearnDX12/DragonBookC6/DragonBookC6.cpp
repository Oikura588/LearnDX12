#include <array>

#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"

#include <DirectXColors.h>
using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

// FrameResources的数目.RenderItem和App都需要这个数据.
static const  int gNumFrameResources=3;
// 顶点信息
struct Vertex
{
    XMFLOAT3 Pos;
    XMFLOAT4 Color;
};
// 常量缓冲区.每个物体有自己的transform信息.
struct ObjectConstants
{
    XMFLOAT4X4 ModelViewProj = MathHelper::Identity4x4();
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
};

// 每次渲染时会更新的缓冲区。
struct PassConstants
{
};

// 以CPU每帧都需更新的资源作为基本元素，包括CmdListAlloc、ConstantBuffer等.
// Draw()中进行绘制时，执行CmdList的Reset函数，来指定当前FrameResource所使用的CmdAlloc,从而将绘制命令存储在每帧的Alloc中.
class FrameResource
{
public:
    FrameResource(ID3D12Device* device,UINT passCount,UINT objectCount)
    {
        device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(CmdAlloc.GetAddressOf())
        );
        ObjectsCB = std::make_unique<UploadBuffer<ObjectConstants>>(device,objectCount,true);
        PassCB = std::make_unique<UploadBuffer<PassConstants>>(device,passCount,true);
    }
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;

    // GPU处理完与此Allocator相关的命令前，不能对其重置，所以每帧都需要保存自己的Alloc.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdAlloc;
    
    // 在GPU处理完此ConstantBuffer相关的命令前，不能对其重置
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectsCB = nullptr;
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;

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
    std::unique_ptr<MeshGeometry> mBoxGeo =nullptr;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    // Shaders.
    ComPtr<ID3DBlob> mvsByteCode = nullptr;
    ComPtr<ID3DBlob> mpsBytecode = nullptr;

    // Pipeline state object.
    ComPtr<ID3D12PipelineState> mPSO = nullptr;

    // 单个物体的常量缓冲区.
    XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();
    // Camera
    float mTheta = 1.5f*XM_PI;
    float mPhi = XM_PIDIV4;
    float mRadius = 5.0f;

    POINT mLastMousePos;
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
    // 创建常量缓冲区堆
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        // 目前仅有一个描述符，即单个box物体的常量缓冲区的描述符.
        heapDesc.NumDescriptors=1;
        // Shader可见，因为需要读取
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        heapDesc.NodeMask = 0;

        ThrowIfFailed( md3dDevice->CreateDescriptorHeap(
            &heapDesc,
            IID_PPV_ARGS(mCbvHeap.GetAddressOf())
        ));
    }
    // 初始化多个帧资源.需要给按照物体个数初始化常量缓冲区，所以需要事先知道物体个数.
    {
        for(int i=0;i<gNumFrameResources;++i)
        {
            mFrameResources.push_back(
                std::make_unique<FrameResource>(md3dDevice.Get(),1,mAllRenderItems.size())
            );
        }
    }
    
    // 初始化常量缓冲区以及View.
    {
       // 由于有多个FrameResource,需要初始化每个FrameResource内、每个物体的的ConstantBuffer，同时把每个物体在buffer中的偏移量保存下来.
        // 所有的FrameResource内的物体偏移是相同的，所以偏移信息存在物体中。
    }
    
    // 初始化RootSignature，把常量缓冲区绑定到GPU上供Shader读取.
    // 把着色器当作一个函数，root signature就是函数签名，资源是参数数据.
    {
        // root signature -> root parameter -> type/table -> range.
        D3D12_DESCRIPTOR_RANGE descRange;
        descRange.NumDescriptors = 1;
        descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        // 思考，这里是指r(0)吗？
        // 这里必须知名base shader register，否则会导致和shader不匹配.
        descRange.BaseShaderRegister = 0;
        descRange.RegisterSpace = 0;
        descRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        
        
        D3D12_ROOT_DESCRIPTOR_TABLE  rootDescTable;
        rootDescTable.pDescriptorRanges = &descRange;
        rootDescTable.NumDescriptorRanges = 1;
        
        D3D12_ROOT_PARAMETER slotRootParameter[1];
        slotRootParameter[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        slotRootParameter[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        slotRootParameter[0].DescriptorTable = rootDescTable;
        
        
        // 创建RootSignature.要使用Blob
        // d3d12规定，必须将根签名的描述布局进行序列化，才可以传入CreateRootSignature方法.
        ComPtr<ID3DBlob> serializedBlob = nullptr,errBlob = nullptr;
        
        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        rootSignatureDesc.NumParameters = 1;
        rootSignatureDesc.pParameters = slotRootParameter;
        rootSignatureDesc.pStaticSamplers = nullptr;
        rootSignatureDesc.NumStaticSamplers = 0;
        
        HRESULT hr =  D3D12SerializeRootSignature(&rootSignatureDesc,D3D_ROOT_SIGNATURE_VERSION_1,serializedBlob.GetAddressOf(),errBlob.GetAddressOf());
        md3dDevice->CreateRootSignature(
            0,
            serializedBlob->GetBufferPointer(),
            serializedBlob->GetBufferSize(),
            IID_PPV_ARGS(mRootSignature.GetAddressOf())
        );
        ThrowIfFailed(hr);


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
        
        hr = D3DCompileFromFile(
            filename.c_str(),
            nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entrypoint.c_str(),
            target.c_str(),
            compileFlags,
            0,
            &mvsByteCode,
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
          &mpsBytecode,
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
            {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
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
            reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),mvsByteCode->GetBufferSize()
            };
        pipelineStateDesc.PS =
            {
            reinterpret_cast<BYTE*>(mpsBytecode->GetBufferPointer()),mpsBytecode->GetBufferSize()
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
            &pipelineStateDesc,IID_PPV_ARGS(&mPSO)
        ));
        
        
    }

    // Build Geometry.和渲染没什么关系了，就是创建buffer并保存起来，绘制的时候用
    {
        std::array<Vertex,8> vertices = {
            Vertex({ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::White) }),
           Vertex({ XMFLOAT3(-1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Black) }),
           Vertex({ XMFLOAT3(+1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Red) }),
           Vertex({ XMFLOAT3(+1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Green) }),
           Vertex({ XMFLOAT3(-1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Blue) }),
           Vertex({ XMFLOAT3(-1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Yellow) }),
           Vertex({ XMFLOAT3(+1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Cyan) }),
           Vertex({ XMFLOAT3(+1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Magenta) })
        };
        std::array<std::uint16_t,36> indices = {
            0,1,2,
            0,2,3,

            4,6,5,
            4,7,6,

            4,5,1,
            4,1,0,

            3,2,6,
            3,6,7,

            1,5,6,
            1,6,2,

            4,0,3,
            4,3,7
        };
        // 创建一个几何体.
        mBoxGeo = std::make_unique<MeshGeometry>();
        mBoxGeo->Name = "Box";

        UINT vbByteSize = vertices.size()*sizeof(Vertex);
        UINT ibByteSize= indices.size()*sizeof(std::uint16_t);
        mBoxGeo->VertexByteStride = sizeof(Vertex);
        mBoxGeo->VertexBufferByteSize = vbByteSize;
        mBoxGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
        mBoxGeo->IndexBufferByteSize = ibByteSize;
        SubmeshGeometry submesh;
        submesh.IndexCount = indices.size();
        submesh.BaseVertexLocation = 0;
        submesh.StartIndexLocation = 0;
        mBoxGeo->DrawArgs["box"]=submesh;

        // 创建顶点缓冲区.

        // 创建默认缓冲区资源.
        D3D12_HEAP_PROPERTIES defaultBufferProperties;
        defaultBufferProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        defaultBufferProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        defaultBufferProperties.MemoryPoolPreference=D3D12_MEMORY_POOL_UNKNOWN;
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
        vbDesc.SampleDesc.Count=1;
        vbDesc.SampleDesc.Quality=0;
        vbDesc.DepthOrArraySize=1;
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
                mBoxGeo->VertexBufferGPU.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST
            )
        );
        mCommandList->ResourceBarrier(
         1,
         &CD3DX12_RESOURCE_BARRIER::Transition(
             mBoxGeo->IndexBufferGPU.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST
         )
         );


        
        // 把数据拷贝到上传缓冲区.使用Subresource.
        // 涉及到的调用:
        // ID3D12Device::GetCopyableFootprints.
        // UploaderBuffer::Map
        // memcpy()?
        // 思考，这里为什么不直接memcpy整个vertexbuffer，而是用subresource来处理.

        BYTE* pData;
        
        HRESULT hr = mBoxGeo->VertexBufferUploader->Map(0,nullptr,reinterpret_cast<void**>(&pData));
        ThrowIfFailed(hr);
        UINT64 SrcOffset = 0;
        UINT64 NumBytes = 0;

        bool bUseSubresource = false;
        if(bUseSubresource)
        {
            // 使用Subresource 进行map
            D3D12_SUBRESOURCE_DATA subResourceData = {};
            subResourceData.pData = vertices.data();
            // 对buffer来说，这两个参数都是buffer的size
            subResourceData.RowPitch = vbByteSize;
            subResourceData.SlicePitch = vbByteSize;
            UINT64 RequiredSize = 0;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layouts[1];
            UINT NumRows[1];
            UINT64 RowSizeInBytes[1];
            D3D12_RESOURCE_DESC Desc = mBoxGeo->VertexBufferGPU.Get()->GetDesc();
            md3dDevice->GetCopyableFootprints(&Desc, 0, 1, 0, Layouts, NumRows, RowSizeInBytes, &RequiredSize);
            SrcOffset = Layouts[0].Offset;
            NumBytes = Layouts[0].Footprint.Width;
            D3D12_MEMCPY_DEST DestData = {
                pData + Layouts[0].Offset, Layouts[0].Footprint.RowPitch, Layouts[0].Footprint.RowPitch * NumRows[0]
            };
            UINT CurrentNumRows = NumRows[0];
            UINT NumSlices = Layouts[0].Footprint.Depth;
            // row by row copy memory.
            for (UINT z = 0; z < NumSlices; ++z)
            {
                BYTE* pDestSlize = reinterpret_cast<BYTE*>(DestData.pData) + DestData.SlicePitch * z;
                const BYTE* srcSlice = reinterpret_cast<const BYTE*>(subResourceData.pData) + subResourceData.SlicePitch
                    * z;
                for (UINT y = 0; y < CurrentNumRows; ++y)
                {
                    memcpy(
                        pDestSlize + DestData.RowPitch * y,
                        srcSlice + subResourceData.RowPitch * y,
                        RowSizeInBytes[0]
                    );
                }
            }
        }
        else
        {
            memcpy(pData,vertices.data(),vbByteSize);
        }
        // Unmap.
        mBoxGeo->VertexBufferUploader->Unmap(0,nullptr);
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
        hr = mBoxGeo->IndexBufferUploader->Map(0,nullptr,reinterpret_cast<void**>(&pData));
        ThrowIfFailed(hr);
        if(bUseSubresource)
        {
            
        }
        else
        {
            memcpy(pData,indices.data(),ibByteSize);
        }
        mBoxGeo->IndexBufferUploader->Unmap(0,nullptr);
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
               mBoxGeo->VertexBufferGPU.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_GENERIC_READ
           )
       );
        mCommandList->ResourceBarrier(
          1,
          &CD3DX12_RESOURCE_BARRIER::Transition(
              mBoxGeo->IndexBufferGPU.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_GENERIC_READ
          )
      );
        
        
        
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
    
    // cmd相关Reset
    mDirectCmdListAlloc->Reset();   // cmdlist 执行完后才能重置,即FlushCommandQuene之后.
    mCommandList->Reset(mDirectCmdListAlloc.Get(),mPSO.Get());  // 传入Queue后就可以重置.

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
    ID3D12DescriptorHeap* descHeaps[] =  {mCbvHeap.Get()};
    mCommandList->SetDescriptorHeaps(_countof(descHeaps),descHeaps);
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    // 绘制一个物体需要绑定两个buffer、设置图元类型、设置常量缓冲区等，把这些绘制一个物体需要的数据整合起来，可以作为RenderItem.
    mCommandList->IASetVertexBuffers(0,1,&mBoxGeo->VertexBufferView());
    mCommandList->IASetIndexBuffer(&mBoxGeo->IndexBufferView());
    mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->SetGraphicsRootDescriptorTable(0,mCbvHeap->GetGPUDescriptorHandleForHeapStart());
    mCommandList->DrawIndexedInstanced(mBoxGeo->DrawArgs["box"].IndexCount,1,0,0,0);

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

    mSwapChain->Present(0,0);
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
