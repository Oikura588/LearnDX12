#pragma once

#include "d3d12.h"

#if defined(__cplusplus)

struct CD3DX12_DEFAULT{};
struct CD3DX12_RESOURCE_BARRIER : public D3D12_RESOURCE_BARRIER
{
    CD3DX12_RESOURCE_BARRIER(){}
    explicit CD3DX12_RESOURCE_BARRIER(const D3D12_RESOURCE_BARRIER &o) :D3D12_RESOURCE_BARRIER(o)
    {
    }
    static inline CD3DX12_RESOURCE_BARRIER Transition(
        _In_ ID3D12Resource* pResource,
        D3D12_RESOURCE_STATES stateBefore,
        D3D12_RESOURCE_STATES stateAfter,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE
    )
    {
        CD3DX12_RESOURCE_BARRIER result;
        ZeroMemory(&result,sizeof(result));
        D3D12_RESOURCE_BARRIER &barrier = result;
        result.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        result.Flags = flags;
        barrier.Transition.pResource = pResource;
        barrier.Transition.StateBefore = stateBefore;
        barrier.Transition.StateAfter = stateAfter;
        barrier.Transition.Subresource = subresource;
        return result;
    }
};

struct CD3DX12_CPU_DESCRIPTOR_HANDLE : public D3D12_CPU_DESCRIPTOR_HANDLE
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE(){}
    explicit CD3DX12_CPU_DESCRIPTOR_HANDLE(const D3D12_CPU_DESCRIPTOR_HANDLE &o):D3D12_CPU_DESCRIPTOR_HANDLE(o){}
    CD3DX12_CPU_DESCRIPTOR_HANDLE(CD3DX12_DEFAULT){ptr = 0;}
    CD3DX12_CPU_DESCRIPTOR_HANDLE(_In_ const D3D12_CPU_DESCRIPTOR_HANDLE &other,INT offsetScaledByIncrementSize)
    {
        InitOffsetted(other,offsetScaledByIncrementSize);
    }
    CD3DX12_CPU_DESCRIPTOR_HANDLE(_In_ const D3D12_CPU_DESCRIPTOR_HANDLE &other,INT offsetInDescriptors,UINT decriptorIncrementSize)
    {
        InitOffsetted(other,offsetInDescriptors,decriptorIncrementSize);
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE& Offset(INT offsetScaledByIncrementSize)
    {
        ptr += offsetScaledByIncrementSize;
        return *this;
    }
    CD3DX12_CPU_DESCRIPTOR_HANDLE& Offset(INT offsetInDescriptors,UINT decriptorIncrementSize)
    {
        ptr += offsetInDescriptors*decriptorIncrementSize;
        return *this;
    }


    inline void InitOffsetted(_In_ const D3D12_CPU_DESCRIPTOR_HANDLE &base,INT offsetScaledByIncrementSize)
    {
        return InitOffsetted(*this,base,offsetScaledByIncrementSize);
    }

    inline void InitOffsetted(_In_ const D3D12_CPU_DESCRIPTOR_HANDLE &base,INT offsetInDescriptors,UINT decriptorIncrementSize)
    {
        return InitOffsetted(*this,base,offsetInDescriptors,decriptorIncrementSize);
    }

    static inline void InitOffsetted(_Out_ D3D12_CPU_DESCRIPTOR_HANDLE &handle,_In_ const D3D12_CPU_DESCRIPTOR_HANDLE &base,INT offsetScaledByIncrementSize)
    {
        handle.ptr = base.ptr + offsetScaledByIncrementSize;
    }
    static inline void InitOffsetted(_Out_ D3D12_CPU_DESCRIPTOR_HANDLE &handle,_In_ const D3D12_CPU_DESCRIPTOR_HANDLE &base,INT offsetInDescriptors,UINT decriptorIncrementSize)
    {
        handle.ptr = base.ptr + offsetInDescriptors*decriptorIncrementSize;
    }
    
    
};

#endif