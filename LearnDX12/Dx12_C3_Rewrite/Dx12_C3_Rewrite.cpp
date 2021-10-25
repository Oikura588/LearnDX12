#include <windows.h>
#ifdef _DEBUG
#include <iostream>
#endif

// dx include and lib.
#include <d3d12.h>
#include <DirectXColors.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")

// using namespace std;

HRESULT result;
// Windows
int window_width = 800;
int window_height = 600;
HWND mhMainWnd;
WNDCLASSEX w = {};

using Microsoft::WRL::ComPtr;
// Dx
ComPtr<ID3D12Device> _dev;
ComPtr<IDXGIFactory6> _dxgiFactory;
UINT _rtvDescriptorSize;
UINT _dsvDescriptorSize;
UINT _cbvDescriptorSize;
// Command object
ComPtr<ID3D12CommandQueue>          _cmdQueue;
ComPtr<ID3D12CommandAllocator>      _cmdListAlloc;
ComPtr<ID3D12GraphicsCommandList>   _cmdList;
// Fence
ComPtr<ID3D12Fence>                 _fence;
UINT64                              _fenceVal=0;
// SwapChain
static const int SwapChainBufferCount = 2;
int _currentBackBuffer = 0;

ComPtr<IDXGISwapChain1> _swapChain;
DXGI_FORMAT _backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
// DescriptorHeap
ComPtr<ID3D12DescriptorHeap> _rtvHeap;
ComPtr<ID3D12DescriptorHeap> _dsvHeap;
// Render target view.
ComPtr<ID3D12Resource> _swapChainBuffer[SwapChainBufferCount];



void DebugOutputFormatString(const char* format,...)
{
#ifdef _DEBUG
    va_list valist;
    va_start(valist,format);
    printf(format,valist);
    va_end(valist);
#endif
}
LRESULT WindowProcedure(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
    if(msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd,msg,wparam,lparam);
}


bool InitWindows()
{
    w.cbSize = sizeof(WNDCLASSEX);
    w.lpfnWndProc = (WNDPROC)WindowProcedure;
    w.lpszClassName = L"DX12Sample";
    w.hInstance = GetModuleHandle(nullptr);

    if(!RegisterClassEx(&w))
    {
        MessageBox(0, L"RegisterClass Failed.", 0, 0);
        return false;
    }
    
    RECT wrc = {0,0,window_width,window_height};
    AdjustWindowRect(&wrc,WS_OVERLAPPEDWINDOW,false);

     mhMainWnd = CreateWindow(w.lpszClassName,
        L"DX12Window",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        wrc.right-wrc.left,
        wrc.bottom-wrc.top,
        nullptr,
        nullptr,
        w.hInstance,
        nullptr
    );
    if( !mhMainWnd )
    {
        MessageBox(0, L"CreateWindow Failed.", 0, 0);
        return false;
    }
    

    ShowWindow(mhMainWnd,SW_SHOW);
    UpdateWindow(mhMainWnd);
    return true;
}

void CreateCommandObject()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    result = _dev->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(_cmdQueue.GetAddressOf()));
    result = _dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(_cmdListAlloc.GetAddressOf()));
    result = _dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,_cmdListAlloc.Get(),nullptr,IID_PPV_ARGS(_cmdList.GetAddressOf()));

    // 首先要关闭.每次用到列表时需要Reset()，但是调用Reset前要close.
    _cmdList->Close();
}
void CreateSwapChain()
{
    // 释放之前的Swapchain,然后重建，运行时就可以修改多重采用的设置等.
    _swapChain.Reset();
    
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width=window_width;
    swapChainDesc.Height=window_height;
    swapChainDesc.Format = _backBufferFormat;
    swapChainDesc.Stereo = false;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER;
    swapChainDesc.SampleDesc.Count= 1;
    swapChainDesc.SampleDesc.Quality =0;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags  = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    // Note: Swap chain uses queue to perform flush.
    result =  _dxgiFactory->CreateSwapChainForHwnd(
        _cmdQueue.Get(),
        mhMainWnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        _swapChain.GetAddressOf()
    );
    
}
void CreateRtvAndDsvHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NodeMask = 0;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    result = _dev->CreateDescriptorHeap(&rtvHeapDesc,IID_PPV_ARGS(_rtvHeap.GetAddressOf()));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NodeMask = 0;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    result = _dev->CreateDescriptorHeap(&dsvHeapDesc,IID_PPV_ARGS(_dsvHeap.GetAddressOf()));
}

void CreateRenderTargetView()
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for(size_t i =0;i<SwapChainBufferCount;++i)
    {
        result = _swapChain->GetBuffer(static_cast<UINT>(i),IID_PPV_ARGS(&_swapChainBuffer[i]));
        _dev->CreateRenderTargetView(_swapChainBuffer[i].Get(),nullptr,handle);
        handle.ptr += _rtvDescriptorSize;
    }
}
// 刷新命令队列.需要执行命令时，调用.
void FlushCommandQueue()
{
    _fenceVal++;
    // 向Queue中添加一条设置新FenceVal的命令.
    // 在GPU执行完Signal前的所有命令前，会一直小于这个值
    result = _cmdQueue->Signal(_fence.Get(),_fenceVal);
    if(_fence->GetCompletedValue()<_fenceVal)
    {
        HANDLE eventHandle = CreateEventEx(nullptr,false,false,EVENT_ALL_ACCESS);
        result = _fence->SetEventOnCompletion(_fenceVal,eventHandle);
        WaitForSingleObject(eventHandle,INFINITE);
        CloseHandle(eventHandle);
    }
}
void OnResize()
{
    
}
D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView()
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvH = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvH.ptr += _currentBackBuffer*_rtvDescriptorSize;
    return rtvH;
}
D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView()
{
    return _dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

bool InitD3D()
{
#ifdef _DEBUG
    // 启动D3D12的调试层
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
    debugController->EnableDebugLayer();
#endif

    // Create dxgi factory.
    result =  CreateDXGIFactory1(IID_PPV_ARGS(&_dxgiFactory));
    // Create device.
    HRESULT hardwareResult = D3D12CreateDevice(
        nullptr,        // 默认adpater
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&_dev)
    );

    // WARP means Windows advanced rasterization platform.硬件创建失败的话用软光栅.
    if(FAILED(hardwareResult))
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter> pWarpAdapter;
        result =  _dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter));
        result =  D3D12CreateDevice(
            pWarpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&_dev)
        );
    }

    // 设备创建完成后，可以为cpu/gpu同步创建Fence。同时因为不同设备上descriptor不同，需要手动获取descriptor的大小
    _dev->CreateFence(
        0,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&_fence)
    );
    _rtvDescriptorSize =  _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    _dsvDescriptorSize =  _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    _cbvDescriptorSize =  _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 先不考虑msaa
    // 创建Command object.
    CreateCommandObject();
    CreateSwapChain();
    CreateRtvAndDsvHeap();

    // Create render target view.
    CreateRenderTargetView();
    
    // 暂时不考虑深度.
    // Create fence.
    
    

    
    return true;
}

void Update(float DeltaTime)
{
    
}
void Draw(float DeltaTime)
{
    result = (_cmdListAlloc->Reset());
    // 通过ExecuteCommandList 将CmdList 加入Queue后，可以重置命令列表.
    result = (_cmdList->Reset(_cmdListAlloc.Get(),nullptr));
    
    // Barrier.
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = _swapChainBuffer[_currentBackBuffer].Get();
    barrier.Transition.Subresource  = 0;
    barrier.Transition.StateBefore  = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter   = D3D12_RESOURCE_STATE_RENDER_TARGET;
    _cmdList->ResourceBarrier(1,&barrier);

    
    _cmdList->ClearRenderTargetView(CurrentBackBufferView(),DirectX::Colors::LightBlue,0,nullptr);
    // 指定要渲染的缓冲区
    _cmdList->OMSetRenderTargets(1,&CurrentBackBufferView(),true,nullptr);


    // Barrier end.
    barrier.Transition.StateBefore  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter   = D3D12_RESOURCE_STATE_PRESENT;
    _cmdList->ResourceBarrier(1,&barrier);
    _cmdList->Close();

    ID3D12CommandList* cmdLists[] = {_cmdList.Get()};
    _cmdQueue->ExecuteCommandLists(1,cmdLists);
    _swapChain->Present(1,0);
    _currentBackBuffer = (_currentBackBuffer+1)%SwapChainBufferCount;

    FlushCommandQueue();
    // 显示画面

}

int Run()
{
    MSG msg = {};

    while (true)
    {
        if(PeekMessage(&msg,nullptr,0,0,PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            Update(0.f);
            Draw(0.f);
        }

        if(msg.message==WM_QUIT)
        {
            break;
        }
    }
    UnregisterClass(w.lpszClassName,w.hInstance);
    return (int)msg.wParam;

}


#ifdef _DEBUG
int main()
{
#else
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int)
{
#endif
    DebugOutputFormatString("Show window test.");
    
    // Debug下开启内存检测，内存泄漏情况
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

    // 初始化窗口
    if (!InitWindows())
    {
        return 0;
    }
    if(!InitD3D())
    {
        return 0;
    }

    // 初始化Dx


    // Run.
    return Run();

    return 0;
}