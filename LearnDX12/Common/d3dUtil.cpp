#include "d3dUtil.h"
#include <comdef.h>

using Microsoft::WRL::ComPtr;

UINT d3dUtil::CalcConstantBufferByteSize(UINT byteSize)
{
    // 常量缓冲区的大小必须是硬件最小分配空间的整数倍.
    // 例如 设bytesize = 300   300+255 = 555 555& ~(255) (屏蔽求和结果中小于255的字节,255=0x00ff)
    return (byteSize+255)&(~255);
}

DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber)
    :ErrorCode(hr)
    ,FunctionName(functionName)
    ,Filename(filename)
    ,LineNumber(lineNumber)
{
    
}

std::wstring DxException::ToString() const
{
    _com_error err(ErrorCode);
    std::wstring msg = err.ErrorMessage();
    return FunctionName + L"failed in "+Filename+L"; line "+std::to_wstring(LineNumber)+L"; error: "+msg;
}
