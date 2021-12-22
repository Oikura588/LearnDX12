#pragma once
#include <d3d12.h>
#include <wrl/client.h>

template<typename T>
class UploadBuffer
{
public:
    UploadBuffer(ID3D12Device* device,UINT elementCount,bool isConstantBuffer);
    

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer;
    BYTE* mMappedData = nullptr;    // 映射资源数据的内存块。用memcpy可以拷贝数据到这个地址，从而完成数据的上传.
    
    bool mIsConstantBuffer;     //判断是否是常量buffer，如果是的话，元素大小要256B对齐，会影响到mElementSize.
    UINT mElementByteSize;
};
