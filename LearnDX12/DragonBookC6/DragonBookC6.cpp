#include "Common/d3dApp.h"
#include "Common/MathHelper.h"
#include "Common/UploadBuffer.h"

#include <DirectXColors.h>
using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

// 顶点信息
struct Vertex
{
    XMFLOAT3 Pos;
    XMFLOAT4 Color;
};
// 常量缓冲 MVP
struct ObjectConstants
{
    XMFLOAT4X4 ModelViewProj = MathHelper::Identity4x4();
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

    void BuildDescriptorHeap();
    void BuildConstantBuffer();
    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildBoxGeometry();
    void BuildPSO();

private:
    ComPtr<ID3D12RootSignature> mRootSignature  = nullptr;
    ComPtr<ID3D12DescriptorHeap> mCbvHeap       = nullptr;

    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
    std::unique_ptr<MeshGeometry> mBoxGeo =nullptr;
    // Shaders.
    ComPtr<ID3DBlob> mvsByteCode = nullptr;
    ComPtr<ID3DBlob> mpsBytecode = nullptr;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    ComPtr<ID3D12PipelineState> mPSO = nullptr;

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
    if(!D3DApp::Initialize())
    {
        return false;
    }
    // 重置命令列表来执行初始化命令
    ThrowIfFailed( mCommandList->Reset(mDirectCmdListAlloc.Get(),nullptr));

    // 初始化中创建了RTV和DSV的，这里创建CBV.
    BuildDescriptorHeap();
    // 创建完heap后创建真正的view.
    BuildConstantBuffer();
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildBoxGeometry();
    BuildPSO();

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
    // 更新Constant Buffer.
    float x = mRadius*sinf(mPhi)*cosf(mTheta);
    float y = mRadius*sinf(mPhi)*sinf(mTheta);
    float z = mRadius*cosf(mPhi);

    // View matrix.
    XMVECTOR pos = XMVectorSet(x,y,z,1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.f,1,0.f,0.f);

    XMMATRIX view = XMMatrixLookAtLH(pos,target,up);
    XMStoreFloat4x4(&mView,view);

    XMMATRIX model = XMLoadFloat4x4(&mWorld);
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX mvp = model*view*proj;

    // 更新常量缓冲区.
    ObjectConstants objectConstants;
    // hlsl是列主序矩阵，DXMath中的矩阵传递时需要转置
    XMStoreFloat4x4(&objectConstants.ModelViewProj,XMMatrixTranspose( mvp));
    mObjectCB->CopyData(0,objectConstants);
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
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(),Colors::LightSteelBlue,0,nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(),D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,1.0f,0,0,nullptr);

    // 指定渲染视图
    mCommandList->OMSetRenderTargets(1,&CurrentBackBufferView(),true,&DepthStencilView());

    // 描述符相关.
    
}

void BoxApp::OnMouseDown(WPARAM btnState, int x, int y)
{
}

void BoxApp::OnMouseUp(WPARAM btnState, int x, int y)
{
}

void BoxApp::OnMouseMove(WPARAM btnState, int x, int y)
{
}

void BoxApp::BuildDescriptorHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NodeMask = 0;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.NumDescriptors = 1;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,IID_PPV_ARGS(&mCbvHeap)));
}

void BoxApp::BuildConstantBuffer()
{
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(),1,true);
    UINT objCBBytesize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();
    // 偏移到常量缓冲区中第i个物体对应的常量数据.这里取i=0
    int boxCBufIdx = 0;
    cbAddress+=boxCBufIdx*objCBBytesize;

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = objCBBytesize;
    md3dDevice->CreateConstantBufferView(
        &cbvDesc,
        mCbvHeap->GetCPUDescriptorHandleForHeapStart()
    );
}

void BoxApp::BuildRootSignature()
{
    // Shader需要的具体资源（cb,tex等）
    CD3DX12_ROOT_PARAMETER slotRootParam[1];
    // 由一组RootParams所定义
    // RootParams可以由 RootConstant/RootDescriptor/DescriptorTable表示.
    // 描述符表是指描述符堆中表示描述符的一段连续区域.

    // 只有一个cbv的描述符表
    CD3DX12_DESCRIPTOR_RANGE cbvTable;
    cbvTable.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
        1,
        0
    );
    slotRootParam[0].InitAsDescriptorTable(
        1,
        &cbvTable
    );
    // 根签名由一组参数组成
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        1,
        slotRootParam,
        0,
        nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    // 创建仅包含一个slot的根签名
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(),
        errorBlob.GetAddressOf()
    );
    if(errorBlob!=nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);
    ThrowIfFailed( md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mRootSignature)
    ));

    
}

void BoxApp::BuildShadersAndInputLayout()
{
    HRESULT hr = S_OK;
    mvsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl",nullptr,"VS","vs_5_0");
    mpsBytecode = d3dUtil::CompileShader(L"Shaders\\color.hlsl",nullptr,"PS","ps_5_0");
    mInputLayout = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0 },
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0 },
    };
}

void BoxApp::BuildBoxGeometry()
{
    std::array<Vertex,8> vertices =
    {
        Vertex({XMFLOAT3(-1.0F,-1.0F,-1.0F),XMFLOAT4(Colors::White)}),
        Vertex({XMFLOAT3(-1.0F,+1.0F,-1.0F),XMFLOAT4(Colors::Black)}),
        Vertex({XMFLOAT3(+1.0F,+1.0F,-1.0F),XMFLOAT4(Colors::Red)}),
        Vertex({XMFLOAT3(+1.0F,-1.0F,-1.0F),XMFLOAT4(Colors::Green)}),
        Vertex({XMFLOAT3(-1.0F,-1.0F,+1.0F),XMFLOAT4(Colors::Blue)}),
        Vertex({XMFLOAT3(-1.0F,+1.0F,+1.0F),XMFLOAT4(Colors::Yellow)}),
        Vertex({XMFLOAT3(+1.0F,+1.0F,+1.0F),XMFLOAT4(Colors::Cyan)}),
        Vertex({XMFLOAT3(+1.0F,-1.0F,+1.0F),XMFLOAT4(Colors::Magenta)}),
    };

    std::array<std::uint16_t,36> indices =
    {
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

    const UINT vbByteSize = sizeof(vertices)*sizeof(Vertex);
    const UINT ibByteSize = sizeof(indices)*sizeof(std::uint16_t);

    mBoxGeo = std::make_unique<MeshGeometry>();
    mBoxGeo->Name = "BoxGeo";

    // vertex buffer.
    D3DCreateBlob(vbByteSize,&(mBoxGeo->VertexBufferCPU));
    CopyMemory(mBoxGeo->VertexBufferCPU->GetBufferPointer(),vertices.data(),vbByteSize);

    D3DCreateBlob(ibByteSize,&(mBoxGeo->IndexBufferCPU));
    CopyMemory(mBoxGeo->IndexBufferCPU->GetBufferPointer(),indices.data(),ibByteSize);

    mBoxGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        vertices.data(),
        vbByteSize,
        mBoxGeo->VertexBufferUploader
    );
    
    mBoxGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        indices.data(),
        ibByteSize,
        mBoxGeo->IndexBufferUploader
    );

    mBoxGeo->VertexByteStride = sizeof(Vertex);
    mBoxGeo->VertexBufferByteSize = vbByteSize;
    mBoxGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
    mBoxGeo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT) indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    mBoxGeo->DrawArgs["box"] = submesh; 
}

void BoxApp::BuildPSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = {mInputLayout.data(),(UINT)mInputLayout.size()};
    psoDesc.pRootSignature = mRootSignature.Get();

    psoDesc.VS = {
        reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
        mvsByteCode->GetBufferSize()
    };
    psoDesc.VS = {
        reinterpret_cast<BYTE*>(mpsBytecode->GetBufferPointer()),
        mpsBytecode->GetBufferSize()
    };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.DSVFormat = mDepthStencilFormat ;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
        &psoDesc,IID_PPV_ARGS(&mPSO)
    ));
    
    
    
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
