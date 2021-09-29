#include <Windows.h>
#ifdef _DEBUG
#include <iostream>
#endif

#include <d3d12.h>
#include <dxgi1_6.h>
#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")

#include <vector>


// Debug.
void DebugOutputFormatString(const char* format,...)
{
#ifdef _DEBUG
    va_list valist;
    va_start(valist,format);
    printf(format,valist);
    va_end(valist);
#endif
}

// Windows 处理消息必要的函数
LRESULT WindowProcedure(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
    if(msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd,msg,wparam,lparam);
}


const unsigned int window_width=1280;
const unsigned int window_height=720;

#ifdef _DEBUG
int main(int argc, char* argv[])
{
#else
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int)
{
#endif
    DebugOutputFormatString("Show window test.");

    // 创建窗口.固定流程.
    WNDCLASSEX w= {};
    w.cbSize = sizeof (WNDCLASSEX);
    w.lpfnWndProc = (WNDPROC)WindowProcedure;
    w.lpszClassName =L"Dx12Sample";
    w.hInstance = GetModuleHandle(nullptr);

    RegisterClassEx(&w);
    RECT wrc = {0,0,window_width,window_height};

    // 校正窗口大小
    AdjustWindowRect(&wrc,WS_OVERLAPPEDWINDOW,false);

    HWND hwnd = CreateWindow(w.lpszClassName,
        L"DX12Test",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        wrc.right-wrc.left,
        wrc.bottom-wrc.top,
        nullptr,
        nullptr,
        w.hInstance,
        nullptr);

    ShowWindow(hwnd,SW_SHOW);

    // D3D12 初始化
    ID3D12Device* _dev = nullptr;
    IDXGIFactory6* _dxgiFactory = nullptr;
    IDXGISwapChain4* _swapChain = nullptr;
   
    auto result =S_OK;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&_dxgiFactory))))
    {
        return -1;
    };

    // 查找显卡设备
    std::vector<IDXGIAdapter*> adapters;
    IDXGIAdapter* tmpAdapter = nullptr;
    for(int i =0;_dxgiFactory->EnumAdapters(i,&tmpAdapter)!=DXGI_ERROR_NOT_FOUND;++i)
    {
        adapters.push_back(tmpAdapter);
    }
    for (auto adpt:adapters)
    {
        DXGI_ADAPTER_DESC adesc={};
        adpt->GetDesc(&adesc);
        std::wstring strDesc = adesc.Description;
        if(strDesc.find(L"NVIDIA")!=std::string::npos)
        {
            tmpAdapter = adpt;
            // break;
        }
    }
    // 创建D3D12 Device
    D3D_FEATURE_LEVEL Levels[]={
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    D3D_FEATURE_LEVEL featureLevel;
    for(auto lv : Levels)
    {
        if( D3D12CreateDevice(tmpAdapter,lv,IID_PPV_ARGS(&_dev)))
        {
            featureLevel=lv;
            break;
        }
    }

    // Command list
    ID3D12CommandAllocator* _cmdAllocator = nullptr;
    ID3D12GraphicsCommandList* _cmdList = nullptr;
    
    result = _dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&_cmdAllocator));
    if(FAILED(result))
    {
        return -2;
    }
    result = _dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,_cmdAllocator,nullptr,IID_PPV_ARGS(&_cmdList));
    if(FAILED(result))
    {
        return -3;
    }
    ID3D12CommandQueue* _cmdQueue = nullptr;
    D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
    cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    // 只是用一个Adapter的话，设成0
    cmdQueueDesc.NodeMask = 0;
    cmdQueueDesc.Priority=D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    // 与CommandList 一致
    cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    result = _dev->CreateCommandQueue(&cmdQueueDesc,IID_PPV_ARGS(&_cmdQueue));
    
    // Swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
    swapchainDesc.Width = window_width;
    swapchainDesc.Height = window_height;
    swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchainDesc.Stereo = false;
    swapchainDesc.SampleDesc.Count = 1;
    swapchainDesc.SampleDesc.Quality=0;
    swapchainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER;
    swapchainDesc.BufferCount = 2;
    swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    result = _dxgiFactory->CreateSwapChainForHwnd(_cmdQueue,hwnd,&swapchainDesc,nullptr,nullptr,(IDXGISwapChain1**)&_swapChain);
    
    
    
    


    

    // 主循环
    MSG msg = {};
    while (true)
    {
        if(PeekMessage(&msg,nullptr,0,0,PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if(msg.message==WM_QUIT)
        {
            break;
        }
    }

    UnregisterClass(w.lpszClassName,w.hInstance);







    
    getchar();
    return 0;


    
}


    
