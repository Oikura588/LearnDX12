#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"

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

private:
    ComPtr<ID3D12RootSignature> mRootSignature  = nullptr;
    ComPtr<ID3D12DescriptorHeap> mCbvHeap       = nullptr;

    // UploadBuffer在创建好后可以方便地每帧更新来修改数据.
    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
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

        md3dDevice->CreateDescriptorHeap(
            &heapDesc,
            IID_PPV_ARGS(mCbvHeap.GetAddressOf())
        );
    }
    
    // 初始化常量缓冲区以及View.
    {
        // 常量缓冲区是Upload类型，可以使用辅助函数来创建.
        mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(),1,true);

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
        cbvDesc.BufferLocation = mObjectCB->Resource()->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

        // 思考，哪里需要CPU描述符，哪里需要gpu描述符?
        // View的地址就是CBV heap的start，因为只有一个.
        md3dDevice->CreateConstantBufferView(
            &cbvDesc,
            mCbvHeap->GetCPUDescriptorHandleForHeapStart()
        );
    }
    
    // 初始化RootSignature，把常量缓冲区绑定到GPU上供Shader读取.
    // 把着色器当作一个函数，root signature就是函数签名，资源是参数数据.
    {
        // root signature -> root parameter -> type/table -> range.
        D3D12_DESCRIPTOR_RANGE descRange;
        descRange.NumDescriptors = 1;
        descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        
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

    // 光栅化状态
    {
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
    mCommandList->ClearRenderTargetView(CurrentBackBufferDescriptor(),Colors::LightSteelBlue,0,nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilDescriptor(),D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,1.0f,0,0,nullptr);

    // 指定渲染视图
    mCommandList->OMSetRenderTargets(1,&CurrentBackBufferDescriptor(),true,&DepthStencilDescriptor());

    // 描述符相关.用来更新常量缓冲区
    ID3D12DescriptorHeap* descHeaps[] =  {mCbvHeap.Get()};
    mCommandList->SetDescriptorHeaps(_countof(descHeaps),descHeaps);
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
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
    FlushCommandQueue();
    
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
