#define GDKS_TRACE_TAG "d3d12_x"
#include "Common.h"

#include "XboxCmdList.h"

#include "Guids.h"
#include "XboxDescHeap.h"
#include "XboxDevice.h"
#include "XboxDeviceInternal.h"

#include <stddef.h>

EXTERN_C const GUID IID_ID3D12CommandList =
	{ 0x7116d91c, 0xe7e4, 0x47ce, { 0xb8, 0xc6, 0xec, 0x81, 0x68, 0xf4, 0x37, 0xe5 } };
EXTERN_C const GUID IID_ID3D12GraphicsCommandList =
	{ 0x5b160d0f, 0xac1b, 0x4185, { 0x8b, 0xa8, 0xb3, 0xae, 0x42, 0xa5, 0xa4, 0x55 } };

struct IXboxCommandList
{
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) = 0;
	virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
	virtual ULONG STDMETHODCALLTYPE Release() = 0;
	virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* size, void* data) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void* data) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* data) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppv) = 0;
	virtual D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE GetType() = 0;
	virtual HRESULT STDMETHODCALLTYPE Close() = 0;
	virtual HRESULT STDMETHODCALLTYPE Reset(ID3D12CommandAllocator* allocator,
	                                        ID3D12PipelineState* initialState) = 0;
	virtual void STDMETHODCALLTYPE ClearState(ID3D12PipelineState* pipelineState) = 0;
	virtual void STDMETHODCALLTYPE DrawInstanced(UINT vertexCountPerInstance, UINT instanceCount,
	                                             UINT startVertexLocation, UINT startInstanceLocation) = 0;
	virtual void STDMETHODCALLTYPE DrawIndexedInstanced(UINT indexCountPerInstance, UINT instanceCount,
	                                                    UINT startIndexLocation, INT baseVertexLocation,
	                                                    UINT startInstanceLocation) = 0;
	virtual void STDMETHODCALLTYPE Dispatch(UINT threadGroupCountX, UINT threadGroupCountY,
	                                        UINT threadGroupCountZ) = 0;
	virtual void STDMETHODCALLTYPE CopyBufferRegion(ID3D12Resource* dstBuffer, UINT64 dstOffset,
	                                                ID3D12Resource* srcBuffer, UINT64 srcOffset,
	                                                UINT64 numBytes) = 0;
	virtual void STDMETHODCALLTYPE CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* dst,
	                                                 UINT dstX, UINT dstY, UINT dstZ,
	                                                 const D3D12_TEXTURE_COPY_LOCATION* src,
	                                                 const D3D12_BOX* srcBox) = 0;
	virtual void STDMETHODCALLTYPE CopyResource(ID3D12Resource* dstResource, ID3D12Resource* srcResource) = 0;
	virtual void STDMETHODCALLTYPE CopyTiles(ID3D12Resource* tiledResource,
	                                         const D3D12_TILED_RESOURCE_COORDINATE* tileRegionStartCoordinate,
	                                         const D3D12_TILE_REGION_SIZE* tileRegionSize,
	                                         ID3D12Resource* buffer, UINT64 bufferStartOffsetInBytes,
	                                         D3D12_TILE_COPY_FLAGS flags) = 0;
	virtual void STDMETHODCALLTYPE ResolveSubresource(ID3D12Resource* dstResource, UINT dstSubresource,
	                                                  ID3D12Resource* srcResource, UINT srcSubresource,
	                                                  DXGI_FORMAT format) = 0;
	virtual void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY primitiveTopology) = 0;
	virtual void STDMETHODCALLTYPE RSSetViewports(UINT numViewports, const D3D12_VIEWPORT* viewports) = 0;
	virtual void STDMETHODCALLTYPE RSSetScissorRects(UINT numRects, const D3D12_RECT* rects) = 0;
	virtual void STDMETHODCALLTYPE OMSetBlendFactor(const FLOAT blendFactor[4]) = 0;
	virtual void STDMETHODCALLTYPE OMSetStencilRef(UINT stencilRef) = 0;
	virtual void STDMETHODCALLTYPE SetPipelineState(ID3D12PipelineState* pipelineState) = 0;
	virtual void STDMETHODCALLTYPE ResourceBarrier(UINT numBarriers,
	                                               const D3D12_RESOURCE_BARRIER* barriers) = 0;
	virtual void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList* commandList) = 0;
	virtual void STDMETHODCALLTYPE BeginQuery(ID3D12QueryHeap* queryHeap, D3D12_QUERY_TYPE type,
	                                          UINT index) = 0;
	virtual void STDMETHODCALLTYPE EndQuery(ID3D12QueryHeap* queryHeap, D3D12_QUERY_TYPE type,
	                                        UINT index) = 0;
	virtual void STDMETHODCALLTYPE ResolveQueryData(ID3D12QueryHeap* queryHeap, D3D12_QUERY_TYPE type,
	                                                UINT startIndex, UINT numQueries,
	                                                ID3D12Resource* destinationBuffer,
	                                                UINT64 alignedDestinationBufferOffset) = 0;
	virtual void STDMETHODCALLTYPE SetPredication(ID3D12Resource* buffer, UINT64 alignedBufferOffset,
	                                              D3D12_PREDICATION_OP operation) = 0;
	virtual void STDMETHODCALLTYPE SetDescriptorHeaps(UINT numDescriptorHeaps,
	                                                  ID3D12DescriptorHeap* const* descriptorHeaps) = 0;
	virtual void STDMETHODCALLTYPE SetComputeRootSignature(ID3D12RootSignature* rootSignature) = 0;
	virtual void STDMETHODCALLTYPE SetGraphicsRootSignature(ID3D12RootSignature* rootSignature) = 0;
	virtual void STDMETHODCALLTYPE SetComputeRootDescriptorTable(UINT rootParameterIndex,
	                                                             D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor) = 0;
	virtual void STDMETHODCALLTYPE SetGraphicsRootDescriptorTable(UINT rootParameterIndex,
	                                                              D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor) = 0;
	virtual void STDMETHODCALLTYPE SetComputeRoot32BitConstant(UINT rootParameterIndex, UINT srcData,
	                                                           UINT destOffsetIn32BitValues) = 0;
	virtual void STDMETHODCALLTYPE SetGraphicsRoot32BitConstant(UINT rootParameterIndex, UINT srcData,
	                                                            UINT destOffsetIn32BitValues) = 0;
	virtual void STDMETHODCALLTYPE SetComputeRoot32BitConstants(UINT rootParameterIndex,
	                                                            UINT num32BitValuesToSet, const void* srcData,
	                                                            UINT destOffsetIn32BitValues) = 0;
	virtual void STDMETHODCALLTYPE SetGraphicsRoot32BitConstants(UINT rootParameterIndex,
	                                                             UINT num32BitValuesToSet, const void* srcData,
	                                                             UINT destOffsetIn32BitValues) = 0;
	virtual void STDMETHODCALLTYPE SetComputeRootConstantBufferView(UINT rootParameterIndex,
	                                                                D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) = 0;
	virtual void STDMETHODCALLTYPE SetGraphicsRootConstantBufferView(UINT rootParameterIndex,
	                                                                 D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) = 0;
	virtual void STDMETHODCALLTYPE SetComputeRootShaderResourceView(UINT rootParameterIndex,
	                                                                D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) = 0;
	virtual void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView(UINT rootParameterIndex,
	                                                                 D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) = 0;
	virtual void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(UINT rootParameterIndex,
	                                                                 D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) = 0;
	virtual void STDMETHODCALLTYPE SetGraphicsRootUnorderedAccessView(UINT rootParameterIndex,
	                                                                  D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) = 0;
	virtual void STDMETHODCALLTYPE IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* view) = 0;
	virtual void STDMETHODCALLTYPE IASetVertexBuffers(UINT startSlot, UINT numViews,
	                                                  const D3D12_VERTEX_BUFFER_VIEW* views) = 0;
	virtual void STDMETHODCALLTYPE SOSetTargets(UINT startSlot, UINT numViews,
	                                            const D3D12_STREAM_OUTPUT_BUFFER_VIEW* views) = 0;
	virtual void STDMETHODCALLTYPE OMSetRenderTargets(UINT numRenderTargetDescriptors,
	                                                  const D3D12_CPU_DESCRIPTOR_HANDLE* renderTargetDescriptors,
	                                                  BOOL rtsSingleHandleToDescriptorRange,
	                                                  const D3D12_CPU_DESCRIPTOR_HANDLE* depthStencilDescriptor) = 0;
	virtual void STDMETHODCALLTYPE ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView,
	                                                     D3D12_CLEAR_FLAGS clearFlags, FLOAT depth,
	                                                     UINT8 stencil, UINT numRects,
	                                                     const D3D12_RECT* rects) = 0;
	virtual void STDMETHODCALLTYPE ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView,
	                                                     const FLOAT colorRGBA[4], UINT numRects,
	                                                     const D3D12_RECT* rects) = 0;
	virtual void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(
		D3D12_GPU_DESCRIPTOR_HANDLE viewGPUHandleInCurrentHeap, D3D12_CPU_DESCRIPTOR_HANDLE viewCPUHandle,
		ID3D12Resource* resource, const UINT values[4], UINT numRects, const D3D12_RECT* rects) = 0;
	virtual void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(
		D3D12_GPU_DESCRIPTOR_HANDLE viewGPUHandleInCurrentHeap, D3D12_CPU_DESCRIPTOR_HANDLE viewCPUHandle,
		ID3D12Resource* resource, const FLOAT values[4], UINT numRects, const D3D12_RECT* rects) = 0;
	virtual void STDMETHODCALLTYPE DiscardResource(ID3D12Resource* resource,
	                                               const D3D12_DISCARD_REGION* region) = 0;
	virtual void STDMETHODCALLTYPE SetMarker(UINT metadata, const void* data, UINT size) = 0;
	virtual void STDMETHODCALLTYPE BeginEvent(UINT metadata, const void* data, UINT size) = 0;
	virtual void STDMETHODCALLTYPE EndEvent() = 0;
	virtual void STDMETHODCALLTYPE ExecuteIndirect(ID3D12CommandSignature* commandSignature,
	                                               UINT maxCommandCount, ID3D12Resource* argumentBuffer,
	                                               UINT64 argumentBufferOffset, ID3D12Resource* countBuffer,
	                                               UINT64 countBufferOffset) = 0;

	virtual void STDMETHODCALLTYPE CopyMemoryToMemoryX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE FillMemoryWith32BitValueX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE FillMemoryWith64BitValueX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE FillMemoryWith128BitValueX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE SetOcclusionQueryControlX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE SetPerfectOcclusionQueriesX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE SetHiStencilControlX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE FlushPipelineX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE OMSetDepthBoundsX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE Write32BitValueBottomOfPipeX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE Write64BitValueBottomOfPipeX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE Wait32BitValueX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE Wait64BitValueX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE SetPixelShaderDepthForceZOrderX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE ExecuteIndirectBundleX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE SetupOrderedAppendCounterX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE WriteGDSX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE ReadGDSX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE DispatchX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE SetHiStencilStateX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE RSSetMSAAParametersX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE DrawIndexedX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE NopX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE CopyTextureRegionX(const D3D12_TEXTURE_COPY_LOCATION* dst,
	                                                  UINT dstX, UINT dstY, UINT dstZ,
	                                                  const D3D12_TEXTURE_COPY_LOCATION* src,
	                                                  const D3D12_BOX* srcBox, UINT flags) = 0;
	virtual void STDMETHODCALLTYPE CopyResourceX(ID3D12Resource* dstResource, ID3D12Resource* srcResource,
	                                             UINT flags) = 0;
	virtual void STDMETHODCALLTYPE SetPredicationBufferX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE AdvancePredicationX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE ExecuteCommandListsX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE Write32BitValueTopOfPipeX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE Write64BitValueTopOfPipeX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE Write32BitValueBottomOfShaderX(void*, void*, void*, void*, void*, void*) = 0;
	// Returns UINT64 in RAX; declared scalar so the ABI matches.
	virtual UINT64 STDMETHODCALLTYPE GetExecutionCommandSizeX(void*, void*, void*, void*, void*, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE CloseBundleX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE KickoffX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE RSSetDepthBiasX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE SetGraphicsShaderLimitsX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE SetComputeShaderLimitsX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE SetResourceCompressionStateX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE PrefetchMemoryX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE PrefetchPipelineStateX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE StartCountersX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE SampleCountersX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE StopCountersX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE TranscodeTextureRegionX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE DeferredPrimitiveBreakBatchX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE DispatchMeshX(void*, void*, void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE SetDeferredPrimitiveBatchBinningX(void*, void*, void*, void*, void*, void*) = 0;
};
static_assert(sizeof(IXboxCommandList) == sizeof(void*));

class XboxCommandList final : public IXboxCommandList
{
public:
	XboxCommandList(ID3D12GraphicsCommandList* real, ID3D12Device* device)
		: mReal(real), mDevice(device)
	{
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
	{
		if (!ppv)
		{
			return E_POINTER;
		}
		if (riid == IID_IUnknown || riid == IID_ID3D12Object || riid == IID_ID3D12DeviceChild ||
		    riid == IID_ID3D12CommandList || riid == IID_ID3D12GraphicsCommandList)
		{
			InterlockedIncrement(&mRefs);
			*ppv = this;
			return S_OK;
		}
		return mReal->QueryInterface(riid, ppv);
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return InterlockedIncrement(&mRefs);
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		LONG refs = InterlockedDecrement(&mRefs);
		if (refs == 0)
		{
			mReal->Release();
			delete this;
		}
		return (ULONG)refs;
	}

	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* size, void* data) override
	{
		return mReal->GetPrivateData(guid, size, data);
	}

	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void* data) override
	{
		return mReal->SetPrivateData(guid, size, data);
	}

	HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* data) override
	{
		return mReal->SetPrivateDataInterface(guid, data);
	}

	HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name) override
	{
		return mReal->SetName(name);
	}

	// Hand back the device we were created from, so the game keeps seeing the
	// Xbox-shaped device rather than the raw desktop one.
	HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppv) override
	{
		if (mDevice && ppv)
		{
			return mDevice->QueryInterface(riid, ppv);
		}
		return mReal->GetDevice(riid, ppv);
	}

	D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE GetType() override
	{
		return mReal->GetType();
	}

	HRESULT STDMETHODCALLTYPE Close() override
	{
		HRESULT hr = mReal->Close();
		if (FAILED(hr))
		{
			static LONG logged = 0;
			if (InterlockedIncrement(&logged) <= 3)
			{
				LOGF("Close FAILED hr=0x%08X - draining validation output:", (unsigned)hr);
				GDKScarlett::D3D12X::DrainInfoQueue(mDevice, "CommandList::Close");
			}
		}
		return hr;
	}

	HRESULT STDMETHODCALLTYPE Reset(ID3D12CommandAllocator* allocator,
	                                ID3D12PipelineState* initialState) override
	{
		return mReal->Reset(allocator, initialState);
	}

	void STDMETHODCALLTYPE ClearState(ID3D12PipelineState* pipelineState) override
	{
		mReal->ClearState(pipelineState);
	}

	void STDMETHODCALLTYPE DrawInstanced(UINT vertexCountPerInstance, UINT instanceCount,
	                                     UINT startVertexLocation, UINT startInstanceLocation) override
	{
		mReal->DrawInstanced(vertexCountPerInstance, instanceCount, startVertexLocation,
		                     startInstanceLocation);
	}

	void STDMETHODCALLTYPE DrawIndexedInstanced(UINT indexCountPerInstance, UINT instanceCount,
	                                            UINT startIndexLocation, INT baseVertexLocation,
	                                            UINT startInstanceLocation) override
	{
		mReal->DrawIndexedInstanced(indexCountPerInstance, instanceCount, startIndexLocation,
		                            baseVertexLocation, startInstanceLocation);
	}

	void STDMETHODCALLTYPE Dispatch(UINT threadGroupCountX, UINT threadGroupCountY,
	                                UINT threadGroupCountZ) override
	{
		mReal->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	}

	void STDMETHODCALLTYPE CopyBufferRegion(ID3D12Resource* dstBuffer, UINT64 dstOffset,
	                                        ID3D12Resource* srcBuffer, UINT64 srcOffset,
	                                        UINT64 numBytes) override
	{
		mReal->CopyBufferRegion(dstBuffer, dstOffset, srcBuffer, srcOffset, numBytes);
	}

	void STDMETHODCALLTYPE CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* dst,
	                                         UINT dstX, UINT dstY, UINT dstZ,
	                                         const D3D12_TEXTURE_COPY_LOCATION* src,
	                                         const D3D12_BOX* srcBox) override
	{
		mReal->CopyTextureRegion(dst, dstX, dstY, dstZ, src, srcBox);
	}

	void STDMETHODCALLTYPE CopyResource(ID3D12Resource* dstResource, ID3D12Resource* srcResource) override
	{
		mReal->CopyResource(dstResource, srcResource);
	}

	void STDMETHODCALLTYPE CopyTiles(ID3D12Resource* tiledResource,
	                                 const D3D12_TILED_RESOURCE_COORDINATE* tileRegionStartCoordinate,
	                                 const D3D12_TILE_REGION_SIZE* tileRegionSize,
	                                 ID3D12Resource* buffer, UINT64 bufferStartOffsetInBytes,
	                                 D3D12_TILE_COPY_FLAGS flags) override
	{
		mReal->CopyTiles(tiledResource, tileRegionStartCoordinate, tileRegionSize, buffer,
		                 bufferStartOffsetInBytes, flags);
	}

	void STDMETHODCALLTYPE ResolveSubresource(ID3D12Resource* dstResource, UINT dstSubresource,
	                                          ID3D12Resource* srcResource, UINT srcSubresource,
	                                          DXGI_FORMAT format) override
	{
		mReal->ResolveSubresource(dstResource, dstSubresource, srcResource, srcSubresource, format);
	}

	void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY primitiveTopology) override
	{
		mReal->IASetPrimitiveTopology(primitiveTopology);
	}

	void STDMETHODCALLTYPE RSSetViewports(UINT numViewports, const D3D12_VIEWPORT* viewports) override
	{
		mReal->RSSetViewports(numViewports, viewports);
	}

	void STDMETHODCALLTYPE RSSetScissorRects(UINT numRects, const D3D12_RECT* rects) override
	{
		mReal->RSSetScissorRects(numRects, rects);
	}

	void STDMETHODCALLTYPE OMSetBlendFactor(const FLOAT blendFactor[4]) override
	{
		mReal->OMSetBlendFactor(blendFactor);
	}

	void STDMETHODCALLTYPE OMSetStencilRef(UINT stencilRef) override
	{
		mReal->OMSetStencilRef(stencilRef);
	}

	void STDMETHODCALLTYPE SetPipelineState(ID3D12PipelineState* pipelineState) override
	{
		mReal->SetPipelineState(pipelineState);
	}

	// Aliasing barriers are dropped: on Xbox these resources live in an
	// aliasable pool, but we back them with committed resources, for which an
	// aliasing barrier is invalid and makes Close() fail with E_INVALIDARG.
	// Committed memory is never aliased, so removing them is safe.
	void STDMETHODCALLTYPE ResourceBarrier(UINT numBarriers,
	                                       const D3D12_RESOURCE_BARRIER* barriers) override
	{
		if (!barriers || numBarriers == 0)
		{
			mReal->ResourceBarrier(numBarriers, barriers);
			return;
		}
		bool anyAliasing = false;
		for (UINT i = 0; i < numBarriers; ++i)
		{
			if (barriers[i].Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING)
			{
				anyAliasing = true;
				break;
			}
		}
		if (!anyAliasing)
		{
			mReal->ResourceBarrier(numBarriers, barriers);
			return;
		}

		D3D12_RESOURCE_BARRIER stackKept[16];
		D3D12_RESOURCE_BARRIER* kept = (numBarriers <= 16)
			? stackKept
			: (D3D12_RESOURCE_BARRIER*)HeapAlloc(GetProcessHeap(), 0,
			                                     numBarriers * sizeof(D3D12_RESOURCE_BARRIER));
		if (!kept)
		{
			return;
		}
		UINT keptCount = 0;
		for (UINT i = 0; i < numBarriers; ++i)
		{
			if (barriers[i].Type != D3D12_RESOURCE_BARRIER_TYPE_ALIASING)
			{
				kept[keptCount++] = barriers[i];
			}
		}
		if (keptCount)
		{
			mReal->ResourceBarrier(keptCount, kept);
		}
		if (kept != stackKept)
		{
			HeapFree(GetProcessHeap(), 0, kept);
		}
		static LONG logged = 0;
		if (InterlockedIncrement(&logged) <= 4)
		{
			LOGF("ResourceBarrier: dropped %u aliasing barrier(s)", numBarriers - keptCount);
		}
	}

	void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList* commandList) override
	{
		mReal->ExecuteBundle(XboxCommandListUnwrap(commandList));
	}

	void STDMETHODCALLTYPE BeginQuery(ID3D12QueryHeap* queryHeap, D3D12_QUERY_TYPE type,
	                                  UINT index) override
	{
		mReal->BeginQuery(queryHeap, type, index);
	}

	void STDMETHODCALLTYPE EndQuery(ID3D12QueryHeap* queryHeap, D3D12_QUERY_TYPE type,
	                                UINT index) override
	{
		mReal->EndQuery(queryHeap, type, index);
	}

	void STDMETHODCALLTYPE ResolveQueryData(ID3D12QueryHeap* queryHeap, D3D12_QUERY_TYPE type,
	                                        UINT startIndex, UINT numQueries,
	                                        ID3D12Resource* destinationBuffer,
	                                        UINT64 alignedDestinationBufferOffset) override
	{
		mReal->ResolveQueryData(queryHeap, type, startIndex, numQueries, destinationBuffer,
		                        alignedDestinationBufferOffset);
	}

	void STDMETHODCALLTYPE SetPredication(ID3D12Resource* buffer, UINT64 alignedBufferOffset,
	                                      D3D12_PREDICATION_OP operation) override
	{
		mReal->SetPredication(buffer, alignedBufferOffset, operation);
	}

	// The game passes back the wrappers we handed it from CreateDescriptorHeap;
	// D3D12Core must receive the real heaps or it dereferences our object as one
	// of its own.
	void STDMETHODCALLTYPE SetDescriptorHeaps(UINT numDescriptorHeaps,
	                                          ID3D12DescriptorHeap* const* descriptorHeaps) override
	{
		if (!descriptorHeaps || numDescriptorHeaps == 0)
		{
			mReal->SetDescriptorHeaps(numDescriptorHeaps, descriptorHeaps);
			return;
		}
		ID3D12DescriptorHeap* stackHeaps[8];
		ID3D12DescriptorHeap** heaps = stackHeaps;
		if (numDescriptorHeaps > ARRAYSIZE(stackHeaps))
		{
			heaps = (ID3D12DescriptorHeap**)HeapAlloc(GetProcessHeap(), 0,
			                                          sizeof(ID3D12DescriptorHeap*) * numDescriptorHeaps);
			if (!heaps)
			{
				return;
			}
		}
		for (UINT i = 0; i < numDescriptorHeaps; ++i)
		{
			heaps[i] = XboxDescriptorHeapUnwrap(descriptorHeaps[i]);
		}
		// GPU descriptor handles are heap-relative and both shader-visible heaps
		// report the same GPU base, so a root-table bind can only be resolved
		// through the heap currently bound on this thread.
		UINT cbvHeaps = 0;
		for (UINT i = 0; i < numDescriptorHeaps; ++i)
		{
			if (!heaps[i])
			{
				continue;
			}
			D3D12_DESCRIPTOR_HEAP_DESC desc = heaps[i]->GetDesc();
			if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
			    (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
			{
				GDKScarlett::D3D12X::NoteBoundCbvHeap(heaps[i]);
				++cbvHeaps;
			}
		}
		if (cbvHeaps > 1)
		{
			static LONG logged = 0;
			if (InterlockedIncrement(&logged) <= 8)
			{
				LOGF("SetDescriptorHeaps: %u CBV/SRV/UAV heaps in ONE call (invalid on desktop)", cbvHeaps);
			}
		}
		mReal->SetDescriptorHeaps(numDescriptorHeaps, heaps);
		if (heaps != stackHeaps)
		{
			HeapFree(GetProcessHeap(), 0, heaps);
		}
	}

	void STDMETHODCALLTYPE SetComputeRootSignature(ID3D12RootSignature* rootSignature) override
	{
		mReal->SetComputeRootSignature(rootSignature);
	}

	void STDMETHODCALLTYPE SetGraphicsRootSignature(ID3D12RootSignature* rootSignature) override
	{
		mReal->SetGraphicsRootSignature(rootSignature);
	}

	void STDMETHODCALLTYPE SetComputeRootDescriptorTable(UINT rootParameterIndex,
	                                                     D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor) override
	{
		BindDescriptorTable(baseDescriptor, rootParameterIndex, true);
		mReal->SetComputeRootDescriptorTable(rootParameterIndex, baseDescriptor);
	}

	void STDMETHODCALLTYPE SetGraphicsRootDescriptorTable(UINT rootParameterIndex,
	                                                      D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor) override
	{
		BindDescriptorTable(baseDescriptor, rootParameterIndex, false);
		mReal->SetGraphicsRootDescriptorTable(rootParameterIndex, baseDescriptor);
	}

	void STDMETHODCALLTYPE SetComputeRoot32BitConstant(UINT rootParameterIndex, UINT srcData,
	                                                   UINT destOffsetIn32BitValues) override
	{
		mReal->SetComputeRoot32BitConstant(rootParameterIndex, srcData, destOffsetIn32BitValues);
	}

	void STDMETHODCALLTYPE SetGraphicsRoot32BitConstant(UINT rootParameterIndex, UINT srcData,
	                                                    UINT destOffsetIn32BitValues) override
	{
		mReal->SetGraphicsRoot32BitConstant(rootParameterIndex, srcData, destOffsetIn32BitValues);
	}

	void STDMETHODCALLTYPE SetComputeRoot32BitConstants(UINT rootParameterIndex, UINT num32BitValuesToSet,
	                                                    const void* srcData,
	                                                    UINT destOffsetIn32BitValues) override
	{
		mReal->SetComputeRoot32BitConstants(rootParameterIndex, num32BitValuesToSet, srcData,
		                                    destOffsetIn32BitValues);
	}

	void STDMETHODCALLTYPE SetGraphicsRoot32BitConstants(UINT rootParameterIndex, UINT num32BitValuesToSet,
	                                                     const void* srcData,
	                                                     UINT destOffsetIn32BitValues) override
	{
		mReal->SetGraphicsRoot32BitConstants(rootParameterIndex, num32BitValuesToSet, srcData,
		                                     destOffsetIn32BitValues);
	}

	void STDMETHODCALLTYPE SetComputeRootConstantBufferView(UINT rootParameterIndex,
	                                                        D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) override
	{
		mReal->SetComputeRootConstantBufferView(rootParameterIndex, bufferLocation);
	}

	void STDMETHODCALLTYPE SetGraphicsRootConstantBufferView(UINT rootParameterIndex,
	                                                         D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) override
	{
		mReal->SetGraphicsRootConstantBufferView(rootParameterIndex, bufferLocation);
	}

	void STDMETHODCALLTYPE SetComputeRootShaderResourceView(UINT rootParameterIndex,
	                                                        D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) override
	{
		mReal->SetComputeRootShaderResourceView(rootParameterIndex, bufferLocation);
	}

	void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView(UINT rootParameterIndex,
	                                                         D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) override
	{
		mReal->SetGraphicsRootShaderResourceView(rootParameterIndex, bufferLocation);
	}

	void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(UINT rootParameterIndex,
	                                                         D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) override
	{
		mReal->SetComputeRootUnorderedAccessView(rootParameterIndex, bufferLocation);
	}

	void STDMETHODCALLTYPE SetGraphicsRootUnorderedAccessView(UINT rootParameterIndex,
	                                                          D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) override
	{
		mReal->SetGraphicsRootUnorderedAccessView(rootParameterIndex, bufferLocation);
	}

	void STDMETHODCALLTYPE IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* view) override
	{
		mReal->IASetIndexBuffer(view);
	}

	void STDMETHODCALLTYPE IASetVertexBuffers(UINT startSlot, UINT numViews,
	                                          const D3D12_VERTEX_BUFFER_VIEW* views) override
	{
		mReal->IASetVertexBuffers(startSlot, numViews, views);
	}

	void STDMETHODCALLTYPE SOSetTargets(UINT startSlot, UINT numViews,
	                                    const D3D12_STREAM_OUTPUT_BUFFER_VIEW* views) override
	{
		mReal->SOSetTargets(startSlot, numViews, views);
	}

	void STDMETHODCALLTYPE OMSetRenderTargets(UINT numRenderTargetDescriptors,
	                                          const D3D12_CPU_DESCRIPTOR_HANDLE* renderTargetDescriptors,
	                                          BOOL rtsSingleHandleToDescriptorRange,
	                                          const D3D12_CPU_DESCRIPTOR_HANDLE* depthStencilDescriptor) override
	{
		mReal->OMSetRenderTargets(numRenderTargetDescriptors, renderTargetDescriptors,
		                          rtsSingleHandleToDescriptorRange, depthStencilDescriptor);
	}

	void STDMETHODCALLTYPE ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView,
	                                             D3D12_CLEAR_FLAGS clearFlags, FLOAT depth, UINT8 stencil,
	                                             UINT numRects, const D3D12_RECT* rects) override
	{
		mReal->ClearDepthStencilView(depthStencilView, clearFlags, depth, stencil, numRects, rects);
	}

	void STDMETHODCALLTYPE ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView,
	                                             const FLOAT colorRGBA[4], UINT numRects,
	                                             const D3D12_RECT* rects) override
	{
		mReal->ClearRenderTargetView(renderTargetView, colorRGBA, numRects, rects);
	}

	void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(
		D3D12_GPU_DESCRIPTOR_HANDLE viewGPUHandleInCurrentHeap, D3D12_CPU_DESCRIPTOR_HANDLE viewCPUHandle,
		ID3D12Resource* resource, const UINT values[4], UINT numRects, const D3D12_RECT* rects) override
	{
		mReal->ClearUnorderedAccessViewUint(viewGPUHandleInCurrentHeap, viewCPUHandle, resource, values,
		                                    numRects, rects);
	}

	void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(
		D3D12_GPU_DESCRIPTOR_HANDLE viewGPUHandleInCurrentHeap, D3D12_CPU_DESCRIPTOR_HANDLE viewCPUHandle,
		ID3D12Resource* resource, const FLOAT values[4], UINT numRects, const D3D12_RECT* rects) override
	{
		mReal->ClearUnorderedAccessViewFloat(viewGPUHandleInCurrentHeap, viewCPUHandle, resource, values,
		                                     numRects, rects);
	}

	void STDMETHODCALLTYPE DiscardResource(ID3D12Resource* resource,
	                                       const D3D12_DISCARD_REGION* region) override
	{
		mReal->DiscardResource(resource, region);
	}

	void STDMETHODCALLTYPE SetMarker(UINT metadata, const void* data, UINT size) override
	{
		mReal->SetMarker(metadata, data, size);
	}

	void STDMETHODCALLTYPE BeginEvent(UINT metadata, const void* data, UINT size) override
	{
		mReal->BeginEvent(metadata, data, size);
	}

	void STDMETHODCALLTYPE EndEvent() override
	{
		mReal->EndEvent();
	}

	void STDMETHODCALLTYPE ExecuteIndirect(ID3D12CommandSignature* commandSignature, UINT maxCommandCount,
	                                       ID3D12Resource* argumentBuffer, UINT64 argumentBufferOffset,
	                                       ID3D12Resource* countBuffer,
	                                       UINT64 countBufferOffset) override
	{
		mReal->ExecuteIndirect(commandSignature, maxCommandCount, argumentBuffer, argumentBufferOffset,
		                       countBuffer, countBufferOffset);
	}

	void STDMETHODCALLTYPE CopyMemoryToMemoryX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE FillMemoryWith32BitValueX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE FillMemoryWith64BitValueX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE FillMemoryWith128BitValueX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE SetOcclusionQueryControlX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE SetPerfectOcclusionQueriesX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE SetHiStencilControlX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE FlushPipelineX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE OMSetDepthBoundsX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE Write32BitValueBottomOfPipeX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE Write64BitValueBottomOfPipeX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE Wait32BitValueX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE Wait64BitValueX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE SetPixelShaderDepthForceZOrderX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE ExecuteIndirectBundleX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE SetupOrderedAppendCounterX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE WriteGDSX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE ReadGDSX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE DispatchX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE SetHiStencilStateX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE RSSetMSAAParametersX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE DrawIndexedX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE NopX(void*, void*, void*, void*, void*, void*) override {}

	// Identical to the desktop calls plus a trailing D3D12XBOX_COPY_FLAGS. These
	// were no-op stubs once, which silently dropped every texture upload routed
	// through them.
	void STDMETHODCALLTYPE CopyTextureRegionX(const D3D12_TEXTURE_COPY_LOCATION* dst,
	                                          UINT dstX, UINT dstY, UINT dstZ,
	                                          const D3D12_TEXTURE_COPY_LOCATION* src,
	                                          const D3D12_BOX* srcBox, UINT) override
	{
		mReal->CopyTextureRegion(dst, dstX, dstY, dstZ, src, srcBox);
	}

	void STDMETHODCALLTYPE CopyResourceX(ID3D12Resource* dstResource, ID3D12Resource* srcResource,
	                                     UINT) override
	{
		mReal->CopyResource(dstResource, srcResource);
	}

	void STDMETHODCALLTYPE SetPredicationBufferX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE AdvancePredicationX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE ExecuteCommandListsX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE Write32BitValueTopOfPipeX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE Write64BitValueTopOfPipeX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE Write32BitValueBottomOfShaderX(void*, void*, void*, void*, void*, void*) override {}

	UINT64 STDMETHODCALLTYPE GetExecutionCommandSizeX(void*, void*, void*, void*, void*, void*) override
	{
		return 0;
	}

	HRESULT STDMETHODCALLTYPE CloseBundleX(void*, void*, void*, void*, void*, void*) override
	{
		return S_OK;
	}

	void STDMETHODCALLTYPE KickoffX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE RSSetDepthBiasX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE SetGraphicsShaderLimitsX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE SetComputeShaderLimitsX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE SetResourceCompressionStateX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE PrefetchMemoryX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE PrefetchPipelineStateX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE StartCountersX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE SampleCountersX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE StopCountersX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE TranscodeTextureRegionX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE DeferredPrimitiveBreakBatchX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE DispatchMeshX(void*, void*, void*, void*, void*, void*) override {}
	void STDMETHODCALLTYPE SetDeferredPrimitiveBatchBinningX(void*, void*, void*, void*, void*, void*) override {}

	ID3D12GraphicsCommandList* Real() const
	{
		return mReal;
	}

private:
	// Copy the live constants into the 256-aligned shadow CBV before the table is
	// bound, and upload any placed-X texel data the table's SRVs point at.
	static void BindDescriptorTable(D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor, UINT rootParameterIndex,
	                                bool isCompute)
	{
		GDKScarlett::D3D12X::ShadowCopyForBind(baseDescriptor.ptr, rootParameterIndex, isCompute);
		UINT dimension = 0;
		UINT format = 0;
		void* resource = nullptr;
		if (GDKScarlett::D3D12X::SrvForGpuHandle(baseDescriptor.ptr, &dimension, &format, &resource) &&
		    resource)
		{
			GDKScarlett::D3D12X::QueuePlacedUpload(resource);
		}
	}

public:
	ID3D12GraphicsCommandList* mReal = nullptr;   // 0x08
	ID3D12Device* mDevice = nullptr;              // 0x10
	volatile LONG mRefs = 1;                      // 0x18
	UINT32 mPad1c = 0;
	void* mReserved20[3] = {};
	void* mPutterCurrent = nullptr;               // 0x38
	void* mReserved40[2] = {};
	void* mPutterLimit = nullptr;                 // 0x50
	void* mReserved58[8] = {};
};

static_assert(offsetof(XboxCommandList, mPutterCurrent) == 0x38);
static_assert(offsetof(XboxCommandList, mPutterLimit) == 0x50);

static const void* GListVtable = nullptr;

ID3D12GraphicsCommandList* XboxCommandListWrap(ID3D12GraphicsCommandList* real, ID3D12Device* device)
{
	if (!real)
	{
		return nullptr;
	}
	XboxCommandList* wrapper = new XboxCommandList(real, device);
	if (!GListVtable)
	{
		GListVtable = *(const void**)wrapper;
	}
	LOGF("XboxCommandListWrap: real %p -> wrapper %p", real, wrapper);
	return (ID3D12GraphicsCommandList*)wrapper;
}

ID3D12GraphicsCommandList* XboxCommandListUnwrap(ID3D12GraphicsCommandList* list)
{
	if (!list)
	{
		return nullptr;
	}
	if (GListVtable && *(const void**)list == GListVtable)
	{
		return ((XboxCommandList*)list)->Real();
	}
	return list;
}
