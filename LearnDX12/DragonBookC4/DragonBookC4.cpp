#include "../Common/d3dApp.h"
#include <DirectXColors.h>

class InitDirect3DApp: public D3DApp
{
public:
    InitDirect3DApp(HINSTANCE hInstance);
    ~InitDirect3DApp();

    virtual bool Initialize() override;
private:
    virtual void OnResize() override;
    void Update(const GameTimer& gt) override;
    void Draw(const GameTimer& gt) override;
};

InitDirect3DApp::InitDirect3DApp(HINSTANCE hInstance)
    :D3DApp(hInstance)
{
}

InitDirect3DApp::~InitDirect3DApp()
{
}

bool InitDirect3DApp::Initialize()
{
    if(!D3DApp::Initialize())
    {
        return false;
    }
    return true;
}

void InitDirect3DApp::OnResize()
{
    D3DApp::OnResize();
}

void InitDirect3DApp::Update(const GameTimer& gt)
{
}

void InitDirect3DApp::Draw(const GameTimer& gt)
{
    // 重复使用记录命令相关内存
    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    // 通过ExecuteCommandList 将CmdList 加入Queue后，可以重置命令列表.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(),nullptr));
    // 转换资源状态，把资源从present转为RT
    mCommandList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET)
    );

    // 设置视口和裁剪矩形，每次重置命令列表后需要重置.
    mCommandList->RSSetViewports(1,&mScreenViewport);
    mCommandList->RSSetScissorRects(1,&mScissorRect);

    // 清楚后台缓冲区和深度缓冲区.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(),DirectX::Colors::LightBlue,0,nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(),D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,1.0f,0,0,nullptr);

    // 指定要渲染的缓冲区
    mCommandList->OMSetRenderTargets(1,&CurrentBackBufferView(),true,&DepthStencilView());

    // 再次对资源转换，从RT转回Present
    mCommandList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT)
    );

    // 完成命令
    ThrowIfFailed(mCommandList->Close());
    // 将CmdList加入队列
    ID3D12CommandList* cmdLists[] = {mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(_countof(cmdLists),cmdLists);

    // 交换缓冲区
    ThrowIfFailed(mSwapChain->Present(0,0));
    mCurrBackBuffer = (mCurrBackBuffer+1)%SwapChainBufferCount;

    // 等待此帧的命令执行完毕
    FlushCommandQueue();
}


int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE prevInstance,PSTR cmdLine,int showCmd)
{
    // Debug下开启内存检测，内存泄漏情况
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif
    try
    {
        InitDirect3DApp theApp(hInstance);
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
