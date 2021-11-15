#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "Waves.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

struct RenderItem
{
    RenderItem() = default;
    // World matrix of the shape.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

    // Nums remain resources needs to sed.
    int NumFramesDirty = gNumFrameResources;

    // Index into GPU constant buffer 
    UINT ObjCBIndex = -1;

    MeshGeometry* Geo = nullptr;

    D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopologyType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
    Opaque = 0,
    Count
};

class LandAndWavesApp : public D3DApp
{
public:
    LandAndWavesApp(HINSTANCE hInstance);
    LandAndWavesApp(const LandAndWavesApp&) = delete;
    LandAndWavesApp& operator=(const LandAndWavesApp&) = delete;
    ~LandAndWavesApp();

    virtual bool Initialize();
protected:
    void OnResize() override;
    void Update(const GameTimer& gt) override;
    void Draw(const GameTimer& gt) override;

    
    void OnMouseDown(WPARAM btnState, int x, int y) override;
    void OnMouseUp(WPARAM btnState, int x, int y) override;
    void OnMouseMove(WPARAM btnState, int x, int y) override;

    void OnKeyboardInput(const GameTimer& gt);
    void UpdateCamera(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMainPassCBs(const GameTimer& gt);
    void UpdateWaves(const GameTimer& gt);

    void BuildRootSignature();
    void BuildShaderAndInputLayout();
    void BuildLandGeometryBuffers();
    void BuildWavesGeometryBuffers();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList,const std::vector<RenderItem*>& ritems);

    float GetHillHeight(float x,float z) const ;
    XMFLOAT3 GetHillsNormal(float x,float z) const;

private:
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrentFrameResources = nullptr;
    int mCurrentFrameResourcesIndex = 0;
    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    std::unordered_map<std::string,std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string,ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string,ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    RenderItem* mWavesRitem = nullptr;

    // List of fall items ;
    std::vector<std::unique_ptr<RenderItem>> mAllRitems ;
    // Render items devided by PSO;
    std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

    std::unique_ptr<Waves> mWaves;

    PassConstants mMainPassCB;

    bool mIsWireframe = false;

    XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f*XM_PI;
    float mPhi = XM_PIDIV2 - 0.1f;
    float mRadius = 50.0f;

    float mSunTheta = 1.25f*XM_PI;
    float mSunPhi = XM_PIDIV4;

    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE prevInstance,PSTR cmdLine,int showCmd)
{
#if defined(DEBUG)|defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF|_CRTDBG_LEAK_CHECK_DF);
#endif
    try
    {
        LandAndWavesApp theApp(hInstance);
        if(!theApp.Initialize())
        {
            return 0;
        }
        return theApp.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
    
}


LandAndWavesApp::LandAndWavesApp(HINSTANCE hInstance)
    :D3DApp(hInstance)
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

LandAndWavesApp::~LandAndWavesApp()
{
}

bool LandAndWavesApp::Initialize()
{
    if(!D3DApp::Initialize())
    {
        return false;
    }
    // Reset the commandlist to prep for initialization commands.
    mCommandList->Reset(mDirectCmdListAlloc.Get(),nullptr);
    mWaves = std::make_unique<Waves>(128,128,1.0,0.03f,4.f,0.2f);

    BuildRootSignature();
    BuildShaderAndInputLayout();
    BuildLandGeometryBuffers();
    BuildWavesGeometryBuffers();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    // Execute the initialize commands.
    mCommandList->Close();
    ID3D12CommandList* cmdLists[] = {mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(_countof(cmdLists),cmdLists);

    FlushCommandQueue();
    return true;
    
}

void LandAndWavesApp::OnResize()
{
    D3DApp::OnResize();

    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25F*MathHelper::Pi,AspectRatio(),1.F,1000.0F);
    XMStoreFloat4x4(&mProj,P);
}

void LandAndWavesApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
    UpdateCamera(gt);

    // cycle through the frame resources array
    mCurrentFrameResourcesIndex = (mCurrentFrameResourcesIndex+1)%gNumFrameResources;
    mCurrentFrameResources = mFrameResources[mCurrentFrameResourcesIndex].get();

    if(mCurrentFrameResources->Fence!=0&&mFence->GetCompletedValue()<mCurrentFrameResources->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFrameResources->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
    UpdateObjectCBs(gt);
    UpdateMainPassCBs(gt);
    UpdateWaves(gt);
    
}

void LandAndWavesApp::Draw(const GameTimer& gt)
{
    // 重置Alloc
    auto CmdListAlloc = mCurrentFrameResources->CmdListAlloc;

    CmdListAlloc->Reset();

    // 重置cmdlist
    if(mIsWireframe)
    {
        mCommandList->Reset(CmdListAlloc.Get(),mPSOs["wireframe"].Get());
    }
    else
    {
        mCommandList->Reset(CmdListAlloc.Get(),mPSOs["opaque"].Get());  
    }

    // 设置视口
    mCommandList->RSSetViewports(1,&mScreenViewport);
    mCommandList->RSSetScissorRects(1,&mScissorRect);

    // 开始绘制
    // 资源转化
    mCommandList->ResourceBarrier(1,&CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET));

    mCommandList->ClearRenderTargetView(CurrentBackBufferView(),Colors::LightSteelBlue,0,nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(),D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,1.0F,0,0,nullptr);

    mCommandList->OMSetRenderTargets(1,&CurrentBackBufferView(),true,&DepthStencilView());

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    // Bind per-pass constant buffer.
    auto passCB = mCurrentFrameResources->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(1,passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(),mRitemLayer[(int)RenderLayer::Opaque]);

    // 绘制结束，资源转换
    mCommandList->ResourceBarrier(1,&CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT));

    mCommandList->Close();

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    mCurrentFrameResources->Fence = ++mCurrentFence;

    mCommandQueue->Signal(mFence.Get(),mCurrentFence);
    
    
}

void LandAndWavesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void LandAndWavesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void LandAndWavesApp::OnMouseMove(WPARAM btnState, int x, int y)
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
        // Make each pixel correspond to 0.2 unit in the scene.
        float dx = 0.2f*static_cast<float>(x - mLastMousePos.x);
        float dy = 0.2f*static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void LandAndWavesApp::OnKeyboardInput(const GameTimer& gt)
{
    if(GetAsyncKeyState('1') & 0x8000)
        mIsWireframe = true;
    else
        mIsWireframe = false;
}

void LandAndWavesApp::UpdateCamera(const GameTimer& gt)
{
    // Convert Spherical to Cartesian coordinates.
    mEyePos.x = mRadius*sinf(mPhi)*cosf(mTheta);
    mEyePos.z = mRadius*sinf(mPhi)*sinf(mTheta);
    mEyePos.y = mRadius*cosf(mPhi);

    // Build the view matrix.
    XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);
}

void LandAndWavesApp::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = mCurrentFrameResources->ObjectCB.get();
    for(auto& e : mAllRitems)
    {
        // Only update the cbuffer data if the constants have changed.  
        // This needs to be tracked per frame resource.
        if(e->NumFramesDirty > 0)
        {
            XMMATRIX world = XMLoadFloat4x4(&e->World);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            // Next FrameResource need to be updated too.
            e->NumFramesDirty--;
        }
    }
}

void LandAndWavesApp::UpdateMainPassCBs(const GameTimer& gt)
{
    XMMATRIX view = XMLoadFloat4x4(&mView);
    XMMATRIX proj = XMLoadFloat4x4(&mProj);

    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
    mMainPassCB.EyePosW = mEyePos;
    mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.NearZ = 1.0f;
    mMainPassCB.FarZ = 1000.0f;
    mMainPassCB.TotalTime = gt.TotalTime();
    mMainPassCB.DeltaTime = gt.DeltaTime();

    auto currPassCB = mCurrentFrameResources->PassCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void LandAndWavesApp::UpdateWaves(const GameTimer& gt)
{
    static float t_base = 0.0f;
    if(mTimer.TotalTime()-t_base>=0.25f)
    {
        t_base+=0.25f;

        int i = MathHelper::Rand(4,mWaves->RowCount()-5);
        int j = MathHelper::Rand(4,mWaves->ColumnCount()-5);

        float r = MathHelper::RandF(0.2f,0.5f);
        mWaves->Disturb(i,j,r);
    }
    // Update the wave simulator.
    mWaves->Update(gt.DeltaTime());

    auto currWavesVB = mCurrentFrameResources->WavesVB.get();
    for(int i =0;i<mWaves->VertexCount();++i)
    {
        Vertex v;
        v.Pos = mWaves->Position(i);
        v.Color = XMFLOAT4(DirectX::Colors::Blue);

        currWavesVB->CopyData(i,v);
    }
    // Set the dynamic VB of the wave renderitem to the current frame VB.
    mWavesRitem->Geo->VertexBufferGPU=currWavesVB->Resource();
    
}

void LandAndWavesApp::BuildRootSignature()
{
    // 一组根参数
    CD3DX12_ROOT_PARAMETER slotRootParameter[2];
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2,slotRootParameter,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,D3D_ROOT_SIGNATURE_VERSION_1,serializedRootSig.GetAddressOf(),errorBlob.GetAddressOf());
    if(errorBlob!=nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);
    md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())
    );
}

void LandAndWavesApp::BuildShaderAndInputLayout()
{
    mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void LandAndWavesApp::BuildLandGeometryBuffers()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f,160.0f,50,50);

    //
    // Extract the vertex elements we are interested and apply the height function to
    // each vertex.  In addition, color the vertices based on their height so we have
    // sandy looking beaches, grassy low hills, and snow mountain peaks.
    //

    std::vector<Vertex> vertices(grid.Vertices.size());
    for(size_t i =0;i<grid.Vertices.size();++i)
    {
        auto& p =grid.Vertices[i].Position;
        vertices[i].Pos = p;
        vertices[i].Pos.y = GetHillHeight(p.x,p.z);

        // Color the vertex based on its height.
        if(vertices[i].Pos.y < -10.0f)
        {
            // Sandy beach color.
            vertices[i].Color = XMFLOAT4(1.0f, 0.96f, 0.62f, 1.0f);
        }
        else if(vertices[i].Pos.y < 5.0f)
        {
            // Light yellow-green.
            vertices[i].Color = XMFLOAT4(0.48f, 0.77f, 0.46f, 1.0f);
        }
        else if(vertices[i].Pos.y < 12.0f)
        {
            // Dark yellow-green.
            vertices[i].Color = XMFLOAT4(0.1f, 0.48f, 0.19f, 1.0f);
        }
        else if(vertices[i].Pos.y < 20.0f)
        {
            // Dark brown.
            vertices[i].Color = XMFLOAT4(0.45f, 0.39f, 0.34f, 1.0f);
        }
        else
        {
            // White snow.
            vertices[i].Color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    const UINT vbByteSize = (UINT)vertices.size()*sizeof(Vertex);

    std::vector<std::uint16_t> indices = grid.GetIndices16();
    const UINT ibByteSize = (UINT)indices.size()*sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "landGeo";

    D3DCreateBlob(vbByteSize,&geo->VertexBufferCPU);
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(),vertices.data(),vbByteSize);

    D3DCreateBlob(ibByteSize,&geo->IndexBufferCPU);
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(),indices.data(),ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),mCommandList.Get(),vertices.data(),vbByteSize,geo->VertexBufferUploader
    );

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),mCommandList.Get(),indices.data(),ibByteSize,geo->IndexBufferUploader
    );

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;
    geo->DrawArgs["grid"] = submesh;
    mGeometries["landGeo"] = std::move(geo);
    
    
}

void LandAndWavesApp::BuildWavesGeometryBuffers()
{
    std::vector<std::uint16_t> indices(3*mWaves->TriangleCount());
    assert(mWaves->VertexCount()<0x0000ffff);
    int m = mWaves->RowCount();
    int n = mWaves->ColumnCount();
    int k =0;
    for (int i=0;i<m-1;++i)
    {
        for (int j =0 ;j<n-1;++j)
        {
            indices[k] = i*n + j;
            indices[k + 1] = i*n + j + 1;
            indices[k + 2] = (i + 1)*n + j;

            indices[k + 3] = (i + 1)*n + j;
            indices[k + 4] = i*n + j + 1;
            indices[k + 5] = (i + 1)*n + j + 1;

            k += 6; // next quad
        }
    }
    UINT vbByteSize = mWaves->VertexCount()*sizeof(Vertex);
    UINT ibByteSize = (UINT)indices.size()* sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "waterGeo";

    geo->VertexBufferCPU = nullptr;
    geo->VertexBufferGPU = nullptr;
    ThrowIfFailed(D3DCreateBlob(ibByteSize,&geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(),indices.data(),ibByteSize);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),mCommandList.Get(),indices.data(),ibByteSize,geo->IndexBufferUploader
    );

    // 顶点Buffer动态更新，这里只需初始化IndexBuffer.
    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["grid"] = submesh;
    mGeometries["waterGeo"] = std::move(geo);

}

void LandAndWavesApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePsoDesc.InputLayout = {mInputLayout.data(),(UINT)mInputLayout.size()};
    opaquePsoDesc.pRootSignature = mRootSignature.Get();
    opaquePsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),mShaders["standardVS"]->GetBufferSize()
    };
    opaquePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),mShaders["opaquePS"]->GetBufferSize()
    };
    opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePsoDesc.SampleMask = UINT_MAX;
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaquePsoDesc.NumRenderTargets = 1;
    opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
    opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

    // PSO for wireframe.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
    opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["wireframe"])));
    

}

void LandAndWavesApp::BuildFrameResources()
{
    for(int i =0;i<gNumFrameResources;++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),1,(UINT)mAllRitems.size(),mWaves->VertexCount()));
    }
}

void LandAndWavesApp::BuildRenderItems()
{
    auto waveRitem = std::make_unique<RenderItem>();
    waveRitem->World = MathHelper::Identity4x4();
    waveRitem->ObjCBIndex = 0;
    waveRitem->Geo = mGeometries["waterGeo"].get();
    waveRitem->PrimitiveTopologyType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    waveRitem->IndexCount = waveRitem->Geo->DrawArgs["grid"].IndexCount;
    waveRitem->StartIndexLocation = waveRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    waveRitem->BaseVertexLocation = waveRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

    mWavesRitem = waveRitem.get();

    mRitemLayer[(int)RenderLayer::Opaque].push_back(waveRitem.get());

    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
    gridRitem->ObjCBIndex = 1;
    gridRitem->Geo = mGeometries["landGeo"].get();
    gridRitem->PrimitiveTopologyType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

    mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());

    mAllRitems.push_back(std::move(waveRitem));
    mAllRitems.push_back(std::move(gridRitem));
}

void LandAndWavesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    auto objectCB = mCurrentFrameResources->ObjectCB->Resource();
    for(size_t i=0;i<ritems.size();++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0,1,&ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveTopologyType);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();
        objCBAddress += ri->ObjCBIndex*objCBByteSize;
        cmdList->SetGraphicsRootConstantBufferView(0,objCBAddress);
        cmdList->DrawIndexedInstanced(ri->IndexCount,1,ri->StartIndexLocation,ri->BaseVertexLocation,0);
    }
    
}

float LandAndWavesApp::GetHillHeight(float x, float z) const
{
    return 0.3f*(z*sinf(0.1f*x)+x*cosf(0.1f*z));
}

XMFLOAT3 LandAndWavesApp::GetHillsNormal(float x, float z) const
{
    // n = (-df/dx, 1, -df/dz)
    XMFLOAT3 n(
        -0.03f*z*cosf(0.1f*x) - 0.3f*cosf(0.1f*z),
        1.0f,
        -0.3f*sinf(0.1f*x) + 0.03f*x*sinf(0.1f*z));

    XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n, unitNormal);

    return n;
}
