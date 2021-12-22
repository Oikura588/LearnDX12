#include "UploadBuffer.h"

template <typename T>
UploadBuffer<T>::UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer)
    :mIsConstantBuffer(isConstantBuffer)
{
    mElementByteSize = sizeof(T);

    // 常量缓冲区大小位256B的整数倍.硬件只能按照m*256B的偏移量和n*256B的数据长度来查看常量数据
    // typedef struct D3D12_CONSTANT_BUFFER_VIEW_DESC
    // {
        // UINT64 OffsetInBytes;   //256的整数倍
        // UINT SizeInBytes;       //256的整数倍
    // };
    // 如果是常量缓冲区，重新计算Buffer大小
    D3D12_HEAP_PROPERTIES heapProperty;
    heapProperty.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProperty.CreationNodeMask = 1;
    heapProperty.VisibleNodeMask = 1;
    heapProperty.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperty.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;

    D3D12_RESOURCE_DESC heapDesc;
    
    // device->CreateCommittedResource(
        // &heapProperty,
        // D3D12_HEAP_FLAG_NONE,
        
    // )
}
