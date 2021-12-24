#pragma once

#include <windows.h>
#include <wrl.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <stdint.h>
#include <sstream>
#include "d3dx12.h"


#include <fstream>
#include <unordered_map>

inline std::wstring AnsiToWString(const std::string& str)
{
    WCHAR buffer[512];
    MultiByteToWideChar(CP_ACP,0,str.c_str(),-1,buffer,512);
    return std::wstring(buffer);
}

// 常用函数
class d3dUtil
{
public:
    // 计算常量缓冲区ByteSize，方便内存对齐.
    static UINT CalcConstantBufferByteSize(UINT byteSize);
};

// 子几何体
struct SubmeshGeometry
{
    // 只需要记录索引数、开始索引位置即可。DrawInstance函数可以自动加上顶点数的偏移.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    INT BaseVertexLocation = 0;

    // Bounding box.
};

// 辅助几何体.
struct MeshGeometry
{
    // 几何体的名称.
    std::string Name;

    // 内存的副本？由于顶点和索引是泛型，所以用blob，用户使用时再转换类型.
    // 思考，作用应该是同时保存CPU、GPU中的buffer.
    Microsoft::WRL::ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> IndexBufferCPU = nullptr;

    // 在GPU中的Buffer为资源.
    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

    // 与缓冲区相关的数据.
    // 由于Buffer格式不确定,几何体要记录自己的大小、步长信息，才能根据偏移正确读取
    UINT VertexByteStride =0;
    UINT VertexBufferByteSize = 0;
    DXGI_FORMAT IndexFormat = DXGI_FORMAT_R16_UINT;
    UINT IndexBufferByteSize = 0;
    
    // 一个MeshGeometry可以存储一组缓冲区的多个几何体(相同顶点、索引类型)
    std::unordered_map<std::string,SubmeshGeometry> DrawArgs;
    
    D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const
    {
        D3D12_VERTEX_BUFFER_VIEW vbv;
        vbv.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();
        vbv.SizeInBytes = VertexBufferByteSize;
        vbv.StrideInBytes = VertexByteStride;
        return vbv;
    }
    D3D12_INDEX_BUFFER_VIEW IndexBufferView() const
    {
        D3D12_INDEX_BUFFER_VIEW ibv;
        ibv.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
        ibv.SizeInBytes = IndexBufferByteSize ;
        ibv.Format = IndexFormat;
        return ibv;
    }
};


// DxException类

class DxException
{
public:
    DxException() = default;
    DxException(HRESULT hr ,const std::wstring& functionName , const std::wstring& filename,int lineNumber );
    std::wstring ToString() const;

    HRESULT ErrorCode = S_OK;
    std::wstring FunctionName;
    std::wstring Filename;
    int LineNumber = -1;
};


// 方便开发的
#ifndef ThrowIfFailed
#define ThrowIfFailed(x)                                            \
{                                                                   \
    HRESULT hr__ = (x);                                             \
    std::wstring wfn = AnsiToWString(__FILE__);                     \
    if(FAILED(hr__)) {throw DxException(hr__,L#x,wfn,__LINE__);}    \
}
#endif

#ifndef ReleaseCom
#define ReleaseCom(x) {if(x){x->Release();x=0;}}
#endif