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
// 常量
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
    void BuildBoxGeometry();
    void BuildPSO();

private:
    ComPtr<ID3D12RootSignature> mRootSignature  = nullptr;
    ComPtr<ID3D12DescriptorHeap> mCbvHeap       = nullptr;

    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
    
};



int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE prevInstance,PSTR cmdLine,int showCmd)
{
    // Debug下开启内存检测，内存泄漏情况
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif
    try
    {
        // if(!theApp.Initialize())
        // {
        //     return 0;
        // }
        // return theApp.Run();
    }
    catch (DxException e)
    {
        MessageBox(nullptr,e.ToString().c_str(),L"HR Failed",MB_OK);
        return 0;
    }
    
}
