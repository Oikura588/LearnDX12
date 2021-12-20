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

#include <fstream>

inline std::wstring AnsiToWString(const std::string& str)
{
    WCHAR buffer[512];
    MultiByteToWideChar(CP_ACP,0,str.c_str(),-1,buffer,512);
    return std::wstring(buffer);
}

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