#include <Windows.h>
#ifdef _DEBUG
#include <iostream>
#endif

#include <d3d12.h>
#include <dxgi1_6.h>
#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")

#include <vector>
#include <DirectXMath.h>

#include <d3dcompiler.h>
#pragma comment(lib,"d3dcompiler.lib")



// Debug.

void EnableDebugLayer()
{
    ID3D12Debug* debugLayer = nullptr;
    auto result = D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer));
    if(debugLayer)
    {
        debugLayer->EnableDebugLayer();
        debugLayer->Release();
    }
}


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


    // D3D12 初始化
#ifdef _DEBUG
    EnableDebugLayer();
#endif
    ID3D12Device* _dev = nullptr;
    IDXGIFactory6* _dxgiFactory = nullptr;
    IDXGISwapChain4* _swapChain = nullptr;
   
    auto result =S_OK;
#ifdef _DEBUG
    result = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG,IID_PPV_ARGS(&_dxgiFactory));
#else
    result = CreateDXGIFactory1(IID_PPV_ARGS(&_dxgiFactory))))
#endif
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

    // Create descriptor heap to restore views.
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc={};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;     //  RenderTargetView
    heapDesc.NodeMask = 0;                              // 多个GPU时才需要指定
    heapDesc.NumDescriptors = 2;                        // 两个Buffer所以是2
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;   // 不需要特殊指定.SRV(ShaderResourceView)或(ConstantBufferView)下，为D3D12_DESCRIPTOR_HEAP_FLAG_VIEW

    ID3D12DescriptorHeap* rtvHeaps= nullptr;
    result = _dev->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&rtvHeaps));
    
   
    // link swapchain and descriptor
    DXGI_SWAP_CHAIN_DESC swcDesc = {};
    result = _swapChain->GetDesc(&swcDesc);

    std::vector<ID3D12Resource*> _backBuffers(swcDesc.BufferCount);
    for(int i=0;i<swcDesc.BufferCount;++i)
    {
        result = _swapChain->GetBuffer(i,IID_PPV_ARGS(&_backBuffers[i]));
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
        handle.ptr+=i*_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        _dev->CreateRenderTargetView(_backBuffers[i],nullptr,handle);
    }

    // Fence
    ID3D12Fence* _fence = nullptr;
    UINT64 _fenceVal = 0;
    result = _dev->CreateFence(_fenceVal,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&_fence));  
    ShowWindow(hwnd,SW_SHOW);


    // 绘制三角形
    DirectX::XMFLOAT3 vertices[] = {
        {-0.4f, -0.7f, 0.0f} , // 左下
         {-0.4f, 0.7f, 0.0f} , // 左上
         { 0.4f, -0.7f, 0.0f} , // 右下
         { 0.4f, 0.7f, 0.0f} , // 右上
    };

    
    // 创建顶点Buffer
    D3D12_HEAP_PROPERTIES heapprop = {};
    heapprop.Type = D3D12_HEAP_TYPE_UPLOAD; //Default: cpu can't access; Upload:cpu can access; Readback: gpu can read from cpu;Custom: must settingother correctly.
    heapprop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapprop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC resdesc = {};
    resdesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resdesc.Width = sizeof(vertices);
    resdesc.Height = 1;
    resdesc.DepthOrArraySize = 1;
    resdesc.MipLevels = 1;
    resdesc.SampleDesc.Count = 1;
    resdesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    resdesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* vertBuffer = nullptr;
    result = _dev->CreateCommittedResource(
        &heapprop,
        D3D12_HEAP_FLAG_NONE,
        &resdesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&vertBuffer)
    );

    // Copy vert data to gpu.
    DirectX::XMFLOAT3 *vertMap = nullptr;
    result = vertBuffer->Map(0,nullptr,(void**)&vertMap);
    std::copy(std::begin(vertices),std::end(vertices),vertMap);
    vertBuffer->Unmap(0,nullptr);

    // vertex buffer view
    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = vertBuffer->GetGPUVirtualAddress();
    vbView.SizeInBytes = sizeof(vertices);
    vbView.StrideInBytes = sizeof(vertices[0]);

    _cmdList->IASetVertexBuffers(0,1,&vbView);

	unsigned short indices[] = {
	0,1,2,
	2,1,3
	};
	ID3D12Resource* idxBuffer = nullptr;
	resdesc.Width = sizeof(indices);

    result = _dev->CreateCommittedResource(
        &heapprop,
        D3D12_HEAP_FLAG_NONE,
        &resdesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&idxBuffer)
    );

    unsigned short *mappedIdx = nullptr;
    idxBuffer->Map(0,nullptr,(void**)&mappedIdx);
    std::copy(std::begin(indices),std::end(indices),mappedIdx);
    idxBuffer->Unmap(0,nullptr);

    D3D12_INDEX_BUFFER_VIEW ibView = {};
    ibView.BufferLocation = idxBuffer->GetGPUVirtualAddress();
    ibView.Format = DXGI_FORMAT_R16_UINT;
    ibView.SizeInBytes = sizeof(indices);



    // 加载Shader并创建ShaderObject.
    ID3DBlob *_vsBlob = nullptr,*_psBlob = nullptr,*errorBlob;

    result = D3DCompileFromFile(L"BasicVertexShader.hlsl",
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "BasicVS","vs_5_0",
        D3DCOMPILE_DEBUG| D3DCOMPILE_SKIP_OPTIMIZATION,
        0,
        &_vsBlob,&errorBlob
        );
    if (FAILED(result))
    {
        if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
        {
            ::OutputDebugStringA("VS shader file not found");
            return 0;
        }
        else
        {
            std::string errstr;
            errstr.resize(errorBlob->GetBufferSize());
            std::copy_n((char*)errorBlob->GetBufferPointer(),errorBlob->GetBufferSize(),errstr.begin());
            errstr+="\n";
			::OutputDebugStringA(errstr.c_str());
        }
    }

	result = D3DCompileFromFile(L"BasicPixelShader.hlsl",
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"BasicPS", "ps_5_0",
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&_psBlob, &errorBlob
	);

	if (FAILED(result))
	{
		if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
		{
			::OutputDebugStringA("PS shader file not found");
			return 0;
		}
		else
		{
			std::string errstr;
			errstr.resize(errorBlob->GetBufferSize());
			std::copy_n((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize(), errstr.begin());
			errstr += "\n";
			::OutputDebugStringA(errstr.c_str());
		}
	}

    // Input layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {
        "POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,
        D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0
        },
    };

    // Graphics pipeline state.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline ={};
    gpipeline.pRootSignature = nullptr;
    gpipeline.VS.pShaderBytecode  = _vsBlob->GetBufferPointer();
    gpipeline.VS.BytecodeLength = _vsBlob->GetBufferSize();
	gpipeline.PS.pShaderBytecode = _psBlob->GetBufferPointer();
	gpipeline.PS.BytecodeLength = _psBlob->GetBufferSize();

    gpipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    gpipeline.RasterizerState.MultisampleEnable = false;
    gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    gpipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; 
    gpipeline.RasterizerState.DepthClipEnable = true;

    gpipeline.BlendState.AlphaToCoverageEnable = false;
    gpipeline.BlendState.IndependentBlendEnable = false;

    D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendDesc = {};
    renderTargetBlendDesc.BlendEnable = false;
    renderTargetBlendDesc.LogicOpEnable = false;
    renderTargetBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    
    gpipeline.BlendState.RenderTarget[0] = renderTargetBlendDesc;

    gpipeline.InputLayout.pInputElementDescs = inputLayout;
    gpipeline.InputLayout.NumElements = sizeof(inputLayout)/sizeof(inputLayout[0]);

    // IBStripCutValue.
    gpipeline.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    gpipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // RenderTarget.
    gpipeline.NumRenderTargets = 1;
    gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;   // 0~1标准化的rgba
    // Anti-aliasing
    gpipeline.SampleDesc.Count=1;
    gpipeline.SampleDesc.Quality = 0;


    // Root signature 
    ID3D12RootSignature* rootSignature = nullptr;
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob *rootSignalBlob = nullptr;
    result = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1_0,
        &rootSignalBlob,
        &errorBlob
        );
    
    result = _dev->CreateRootSignature(0,rootSignalBlob->GetBufferPointer(),rootSignalBlob->GetBufferSize(),IID_PPV_ARGS(&rootSignature));

    gpipeline.pRootSignature = rootSignature;


	// 创建Pipeline state.
	ID3D12PipelineState* _pipelinestate = nullptr;
	result = _dev->CreateGraphicsPipelineState(&gpipeline, IID_PPV_ARGS(&_pipelinestate));

    // Viewport 
    D3D12_VIEWPORT viewport = {};
    viewport.Width = window_width;
    viewport.Height = window_height;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.MaxDepth = 1.0f;
    viewport.MinDepth = 0.0f;

    D3D12_RECT scissorrect = {};
    scissorrect.top=0;
    scissorrect.left = 0;
    scissorrect.right = scissorrect.left+window_width;
    scissorrect.bottom = scissorrect.top+window_height;

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
        // back buffer idx
        auto bbIdx = _swapChain->GetCurrentBackBufferIndex();
        // Barrier
        D3D12_RESOURCE_BARRIER BarrierDesc = {};
        BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        BarrierDesc.Transition.pResource = _backBuffers[bbIdx];
        BarrierDesc.Transition.Subresource = 0;
        BarrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        BarrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        _cmdList->ResourceBarrier(1,&BarrierDesc);
        
        auto rtvH = rtvHeaps -> GetCPUDescriptorHandleForHeapStart();
        rtvH.ptr+=bbIdx*_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        _cmdList->OMSetRenderTargets(1,&rtvH,true,nullptr);

        // 设置Color 并clear
        float clearColor[] = {0.0f,0.0f,0.0f,1.0f};
        _cmdList->ClearRenderTargetView(rtvH,clearColor,0,nullptr);


		_cmdList->SetPipelineState(_pipelinestate);
		_cmdList->SetGraphicsRootSignature(rootSignature);
		_cmdList->RSSetViewports(1, &viewport);
		_cmdList->RSSetScissorRects(1, &scissorrect);
		_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		_cmdList->IASetVertexBuffers(0, 1, &vbView);
        _cmdList->IASetIndexBuffer(&ibView);
		_cmdList->DrawIndexedInstanced(6, 1, 0, 0,0);



      
        // Set barrier before close
        BarrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        BarrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        _cmdList->ResourceBarrier(1,&BarrierDesc);
        _cmdList->Close();

        // 执行 command list
        ID3D12CommandList* cmdLists[] = {_cmdList};
        _cmdQueue->ExecuteCommandLists(1,cmdLists);

        // fence wait
        _cmdQueue->Signal(_fence,++_fenceVal);
        // wait gpu
        if(_fence->GetCompletedValue()!=_fenceVal)
        {
            auto event = CreateEvent(nullptr,false,false,nullptr);
            _fence->SetEventOnCompletion(_fenceVal,event);
            WaitForSingleObject(event,INFINITE);
            CloseHandle(event);
        }

        _cmdAllocator->Reset();
        _cmdList->Reset(_cmdAllocator,nullptr);

        // 显示画面
        _swapChain->Present(1,0);
    }

    UnregisterClass(w.lpszClassName,w.hInstance);







    
    getchar();
    return 0;


    
}


    
