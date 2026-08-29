#define GDKS_TRACE_TAG "d3d12_x"
#include "Common.h"
#include <stdio.h>
#include <string.h>
#include "XboxDevice.h"
#include "XboxDeviceInternal.h"
#include "Guids.h"
#include "XboxDxgiDevice.h"
#include "XboxDescHeap.h"
#include "XboxCmdQueue.h"
#include "XboxCmdList.h"
#include "Gcn/GcnDecoder.h"
#include "ShaderRecompiler.h"
#include <dxgi.h>
#include <dxgi1_2.h>
#include <vector>
#include <map>
#include <set>
#include <string>

struct BufRec
{
	D3D12_GPU_VIRTUAL_ADDRESS va;
	UINT64 size;
	uint8_t* cpu;
};

struct GpuBufRange
{
	D3D12_GPU_VIRTUAL_ADDRESS va;
	UINT64 size;
	void* res;
};

struct Shadow
{
	ID3D12Resource* res;
	uint8_t* cpu;
	D3D12_GPU_VIRTUAL_ADDRESS va;
	UINT size;
};

struct HeapRec
{
	UINT64 gpuBase;
	SIZE_T cpuBase;
	UINT inc;
	UINT num;
	void* heap;
};

struct CbvRec
{
	D3D12_GPU_VIRTUAL_ADDRESS sourceVA;
	UINT size;
	uint8_t* shadowCpu;
	LONG* pending;
};

struct PlacedRec
{
	UINT64 va;
	UINT64 width;
	UINT height;
	UINT fmt;
	UINT dim;
	UINT state;
	UINT64 lastUpload;
};

struct XGComputerVtbl
{
	ULONG(STDMETHODCALLTYPE* AddRef)(void*);
	ULONG(STDMETHODCALLTYPE* Release)(void*);
	HRESULT(STDMETHODCALLTYPE* GetResourceLayout)(void*, void*);
	UINT64(STDMETHODCALLTYPE* GetResourceSizeBytes)(void*);
	UINT64(STDMETHODCALLTYPE* GetResourceBaseAlignmentBytes)(void*);
	UINT64(STDMETHODCALLTYPE* GetMipLevelOffsetBytes)(void*, UINT, UINT);
	UINT64(STDMETHODCALLTYPE* GetTexelElementOffsetBytes)(void*, UINT, UINT, UINT64, UINT, UINT,
	                                                      UINT);
	HRESULT(STDMETHODCALLTYPE* GetTexelCoordinate)(void*, UINT64, UINT*, UINT*, UINT64*, UINT*,
	                                               UINT*, UINT*);
	HRESULT(STDMETHODCALLTYPE* CopyIntoSubresource)(void*, void*, UINT, UINT, const void*, UINT,
	                                                UINT);
	HRESULT(STDMETHODCALLTYPE* CopyFromSubresource)(void*, void*, UINT, UINT, const void*, UINT,
	                                                UINT);
};

struct XGComputer
{
	const XGComputerVtbl* vtbl;
};

struct XG_SAMPLE_DESC_x
{
	UINT Count;
	UINT Quality;
};

struct XG_TEXTURE2D_DESC_x
{
	UINT Width, Height, MipLevels, ArraySize, Format;
	XG_SAMPLE_DESC_x SampleDesc;
	UINT Usage, BindFlags, CPUAccessFlags, MiscFlags, ESRAMOffsetBytes, ESRAMUsageBytes, TileMode,
	    Pitch;
};

typedef UINT(WINAPI* PFN_XGComputeOptimalTileMode)(UINT dim, UINT fmt, UINT w, UINT h,
                                                   UINT depthOrArray, UINT samples, UINT bindFlags,
                                                   UINT miscFlags);

typedef HRESULT(WINAPI* PFN_XGCreateTexture2DComputer)(const XG_TEXTURE2D_DESC_x*, XGComputer**);

struct D3D12XBOX_COMMAND_QUEUE_DESC
{
	D3D12_COMMAND_LIST_TYPE Type;
	D3D12_COMMAND_QUEUE_FLAGS Flags;
	UINT EngineOrPipeIndex;
	UINT QueueIndex;
};

struct D3D12XBOX_COMMAND_LIST_DESC
{
	D3D12_COMMAND_LIST_TYPE Type;
	UINT Flags;
	UINT NodeMask;
	UINT ResourceBarrierBatchSize;
};

struct FrameObjList
{
	UINT Count;
	HANDLE* pObjects;
};

struct IXboxDevice
{
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) = 0;
	virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
	virtual ULONG STDMETHODCALLTYPE Release() = 0;
	virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) = 0;
	virtual UINT STDMETHODCALLTYPE GetNodeCount() = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC* pDesc, REFIID riid, void** ppCommandQueue) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type, REFIID riid, void** ppCommandAllocator) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateCommandList(UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* pCommandAllocator, ID3D12PipelineState* pInitialState, REFIID riid, void** ppCommandList) = 0;
	virtual HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D12_FEATURE Feature, void* pFeatureSupportData, UINT FeatureSupportDataSize) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc, REFIID riid, void** ppvHeap) = 0;
	virtual UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateRootSignature(UINT nodeMask, const void* pBlobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void** ppvRootSignature) = 0;
	virtual void STDMETHODCALLTYPE CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) = 0;
	virtual void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) = 0;
	virtual void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Resource* pResource, ID3D12Resource* pCounterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) = 0;
	virtual void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Resource* pResource, const D3D12_RENDER_TARGET_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) = 0;
	virtual void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Resource* pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) = 0;
	virtual void STDMETHODCALLTYPE CreateSampler(const D3D12_SAMPLER_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) = 0;
	virtual void STDMETHODCALLTYPE CopyDescriptors(UINT NumDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts, const UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts, const UINT* pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) = 0;
	virtual void STDMETHODCALLTYPE CopyDescriptorsSimple(UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart, D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) = 0;
	virtual D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE GetResourceAllocationInfo(IXboxDevice* device, UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC* pResourceDescs) = 0;
	virtual D3D12_HEAP_PROPERTIES* STDMETHODCALLTYPE GetCustomHeapProperties(IXboxDevice* device, UINT nodeMask, D3D12_HEAP_TYPE heapType) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateCommittedResource(const D3D12_HEAP_PROPERTIES* pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource, void** ppvResource) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateHeap(const D3D12_HEAP_DESC* pDesc, REFIID riid, void** ppvHeap) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Heap* pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateReservedResource(const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateSharedHandle(ID3D12DeviceChild* pObject, const SECURITY_ATTRIBUTES* pAttributes, DWORD Access, LPCWSTR Name, HANDLE* pHandle) = 0;
	virtual HRESULT STDMETHODCALLTYPE OpenSharedHandle(HANDLE NTHandle, REFIID riid, void** ppvObj) = 0;
	virtual HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(LPCWSTR Name, DWORD Access, HANDLE* pNTHandle) = 0;
	virtual HRESULT STDMETHODCALLTYPE MakeResident(UINT NumObjects, ID3D12Pageable* const* ppObjects) = 0;
	virtual HRESULT STDMETHODCALLTYPE Evict(UINT NumObjects, ID3D12Pageable* const* ppObjects) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateFence(UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void** ppFence) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() = 0;
	virtual void STDMETHODCALLTYPE GetCopyableFootprints(const D3D12_RESOURCE_DESC* pResourceDesc, UINT FirstSubresource, UINT NumSubresources, UINT64 BaseOffset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts, UINT* pNumRows, UINT64* pRowSizeInBytes, UINT64* pTotalBytes) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateQueryHeap(const D3D12_QUERY_HEAP_DESC* pDesc, REFIID riid, void** ppvHeap) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL Enable) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC* pDesc, ID3D12RootSignature* pRootSignature, REFIID riid, void** ppvCommandSignature) = 0;
	virtual void STDMETHODCALLTYPE GetResourceTiling(ID3D12Resource* pTiledResource, UINT* pNumTilesForEntireResource, D3D12_PACKED_MIP_INFO* pPackedMipDesc, D3D12_TILE_SHAPE* pStandardTileShapeForNonPackedMips, UINT* pNumSubresourceTilings, UINT FirstSubresourceTilingToGet, D3D12_SUBRESOURCE_TILING* pSubresourceTilingsForNonPackedMips) = 0;
	virtual LUID STDMETHODCALLTYPE GetAdapterLuid() = 0;
	virtual HRESULT STDMETHODCALLTYPE CreatePlacedResourceX(D3D12_GPU_VIRTUAL_ADDRESS ResourceLocation, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateComponentPlacedResourceX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreatePlacedShaderResourceViewX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreatePlacedUnorderedAccessViewX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateCommandQueueX(const D3D12XBOX_COMMAND_QUEUE_DESC* pDesc, REFIID riid, void** ppCommandQueue) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetDriverHintX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetHangCallbacksX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE ReportGpuHangX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetDebugFlagsX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual UINT STDMETHODCALLTYPE GetDebugFlagsX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetDebugCallbackX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateDerivedGraphicsPipelineStateX(ID3D12PipelineState* pSrcPipelineState, UINT NumDescs, const void* pDescs, REFIID riid, void** ppDerivedPipelineState) = 0;
	virtual HRESULT STDMETHODCALLTYPE SerializeGraphicsPipelineStateX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE DeserializeGraphicsPipelineStateX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE EnableManualGraphicsTLBInvalidationX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetDefaultMSAAParametersX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineStateX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateSamplerX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateComputePipelineStateX(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pBaseDesc, UINT NumExtendedDescs, const void*  a3, REFIID riid, void** ppPipelineState) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateDerivedComputePipelineStateX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE RegisterCustomFenceLocationX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE UnregisterCustomFenceLocationX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE SerializeComputePipelineStateX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE DeserializeComputePipelineStateX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetGpuHardwareConfigurationX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetDebugErrorFilterX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreatePlacedRawShaderResourceViewX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreatePlacedRawUnorderedAccessViewX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetGpuMemoryPriorityX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateCommandListX(const D3D12XBOX_COMMAND_LIST_DESC* pDesc, ID3D12CommandAllocator* pAllocator, ID3D12PipelineState* pInitialState, REFIID riid, void** ppCommandList) = 0;
	virtual HRESULT STDMETHODCALLTYPE RegisterPagePoolX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE UnregisterPagePoolX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateCommittedResourceX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateComponentPlacedResourceX1(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetFrameIntervalX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE ScheduleFrameEventX(UINT Type, UINT IntervalOffsetUs, FrameObjList* pList, UINT Flags, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE WaitFrameEventX(UINT Type, UINT TimeOutInMs, void*  a3, UINT Flags, UINT64* pToken) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetFrameStatisticsX(UINT64 Token, UINT TypeSet, UINT* pCount, BYTE* pStatistics, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateCounterSetX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateCommittedOpaqueResourceX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreatePlacedOpaqueResourceX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetOpaqueResourceAllocationInfoX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateFeedbackUnorderedAccessViewX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreatePipelineLibrary(const void* pLibraryBlob, SIZE_T BlobLength, REFIID riid, void** ppPipelineLibrary) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetEventOnMultipleFenceCompletion(ID3D12Fence* const* ppFences, const UINT64* pFenceValues, UINT NumFences, D3D12_MULTIPLE_FENCE_WAIT_FLAGS Flags, HANDLE hEvent) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetResidencyPriority(UINT NumObjects, ID3D12Pageable* const* ppObjects, const D3D12_RESIDENCY_PRIORITY* pPriorities) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreatePipelineState(const D3D12_PIPELINE_STATE_STREAM_DESC* pDesc, REFIID riid, void** ppPipelineState) = 0;
	virtual HRESULT STDMETHODCALLTYPE OpenExistingHeapFromAddress(const void* pAddress, REFIID riid, void** ppvHeap) = 0;
	virtual HRESULT STDMETHODCALLTYPE OpenExistingHeapFromFileMapping(HANDLE hFileMapping, REFIID riid, void** ppvHeap) = 0;
	virtual HRESULT STDMETHODCALLTYPE RegisterPagePool2X(void* a1, void* a2, void* a3, void* a4) = 0;
	virtual HRESULT STDMETHODCALLTYPE EnqueueMakeResident(D3D12_RESIDENCY_FLAGS Flags, UINT NumObjects, ID3D12Pageable* const* ppObjects, ID3D12Fence* pFenceToSignal, UINT64 FenceValueToSignal) = 0;};

static_assert(sizeof(IXboxDevice) == sizeof(void*),
              "IXboxDevice must stay a pure vtable: no data members, single inheritance");

class XboxDevice final : public IXboxDevice
{
public:
	explicit XboxDevice(ID3D12Device* realDevice) : real(realDevice) {}

	ID3D12Device* real = nullptr;
	volatile LONG refs = 1;
	IUnknown* dxgiDevice = nullptr;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
	ULONG STDMETHODCALLTYPE AddRef() override;
	ULONG STDMETHODCALLTYPE Release() override;
	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) override;
	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) override;
	HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) override;
	HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override;
	UINT STDMETHODCALLTYPE GetNodeCount() override;
	HRESULT STDMETHODCALLTYPE CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC* pDesc, REFIID riid, void** ppCommandQueue) override;
	HRESULT STDMETHODCALLTYPE CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type, REFIID riid, void** ppCommandAllocator) override;
	HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) override;
	HRESULT STDMETHODCALLTYPE CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) override;
	HRESULT STDMETHODCALLTYPE CreateCommandList(UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* pCommandAllocator, ID3D12PipelineState* pInitialState, REFIID riid, void** ppCommandList) override;
	HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D12_FEATURE Feature, void* pFeatureSupportData, UINT FeatureSupportDataSize) override;
	HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc, REFIID riid, void** ppvHeap) override;
	UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) override;
	HRESULT STDMETHODCALLTYPE CreateRootSignature(UINT nodeMask, const void* pBlobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void** ppvRootSignature) override;
	void STDMETHODCALLTYPE CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
	void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
	void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Resource* pResource, ID3D12Resource* pCounterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
	void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Resource* pResource, const D3D12_RENDER_TARGET_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
	void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Resource* pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
	void STDMETHODCALLTYPE CreateSampler(const D3D12_SAMPLER_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
	void STDMETHODCALLTYPE CopyDescriptors(UINT NumDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts, const UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts, const UINT* pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override;
	void STDMETHODCALLTYPE CopyDescriptorsSimple(UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart, D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override;
	D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE GetResourceAllocationInfo(IXboxDevice* device, UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC* pResourceDescs) override;
	D3D12_HEAP_PROPERTIES* STDMETHODCALLTYPE GetCustomHeapProperties(IXboxDevice* device, UINT nodeMask, D3D12_HEAP_TYPE heapType) override;
	HRESULT STDMETHODCALLTYPE CreateCommittedResource(const D3D12_HEAP_PROPERTIES* pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource, void** ppvResource) override;
	HRESULT STDMETHODCALLTYPE CreateHeap(const D3D12_HEAP_DESC* pDesc, REFIID riid, void** ppvHeap) override;
	HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Heap* pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) override;
	HRESULT STDMETHODCALLTYPE CreateReservedResource(const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) override;
	HRESULT STDMETHODCALLTYPE CreateSharedHandle(ID3D12DeviceChild* pObject, const SECURITY_ATTRIBUTES* pAttributes, DWORD Access, LPCWSTR Name, HANDLE* pHandle) override;
	HRESULT STDMETHODCALLTYPE OpenSharedHandle(HANDLE NTHandle, REFIID riid, void** ppvObj) override;
	HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(LPCWSTR Name, DWORD Access, HANDLE* pNTHandle) override;
	HRESULT STDMETHODCALLTYPE MakeResident(UINT NumObjects, ID3D12Pageable* const* ppObjects) override;
	HRESULT STDMETHODCALLTYPE Evict(UINT NumObjects, ID3D12Pageable* const* ppObjects) override;
	HRESULT STDMETHODCALLTYPE CreateFence(UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void** ppFence) override;
	HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override;
	void STDMETHODCALLTYPE GetCopyableFootprints(const D3D12_RESOURCE_DESC* pResourceDesc, UINT FirstSubresource, UINT NumSubresources, UINT64 BaseOffset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts, UINT* pNumRows, UINT64* pRowSizeInBytes, UINT64* pTotalBytes) override;
	HRESULT STDMETHODCALLTYPE CreateQueryHeap(const D3D12_QUERY_HEAP_DESC* pDesc, REFIID riid, void** ppvHeap) override;
	HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL Enable) override;
	HRESULT STDMETHODCALLTYPE CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC* pDesc, ID3D12RootSignature* pRootSignature, REFIID riid, void** ppvCommandSignature) override;
	void STDMETHODCALLTYPE GetResourceTiling(ID3D12Resource* pTiledResource, UINT* pNumTilesForEntireResource, D3D12_PACKED_MIP_INFO* pPackedMipDesc, D3D12_TILE_SHAPE* pStandardTileShapeForNonPackedMips, UINT* pNumSubresourceTilings, UINT FirstSubresourceTilingToGet, D3D12_SUBRESOURCE_TILING* pSubresourceTilingsForNonPackedMips) override;
	LUID STDMETHODCALLTYPE GetAdapterLuid() override;
	HRESULT STDMETHODCALLTYPE CreatePlacedResourceX(D3D12_GPU_VIRTUAL_ADDRESS ResourceLocation, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) override;
	HRESULT STDMETHODCALLTYPE CreateComponentPlacedResourceX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreatePlacedShaderResourceViewX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreatePlacedUnorderedAccessViewX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreateCommandQueueX(const D3D12XBOX_COMMAND_QUEUE_DESC* pDesc, REFIID riid, void** ppCommandQueue) override;
	HRESULT STDMETHODCALLTYPE SetDriverHintX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE SetHangCallbacksX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE ReportGpuHangX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE SetDebugFlagsX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	UINT STDMETHODCALLTYPE GetDebugFlagsX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE SetDebugCallbackX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreateDerivedGraphicsPipelineStateX(ID3D12PipelineState* pSrcPipelineState, UINT NumDescs, const void* pDescs, REFIID riid, void** ppDerivedPipelineState) override;
	HRESULT STDMETHODCALLTYPE SerializeGraphicsPipelineStateX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE DeserializeGraphicsPipelineStateX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE EnableManualGraphicsTLBInvalidationX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE GetDefaultMSAAParametersX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineStateX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreateSamplerX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreateComputePipelineStateX(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pBaseDesc, UINT NumExtendedDescs, const void*  a3, REFIID riid, void** ppPipelineState) override;
	HRESULT STDMETHODCALLTYPE CreateDerivedComputePipelineStateX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE RegisterCustomFenceLocationX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE UnregisterCustomFenceLocationX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE SerializeComputePipelineStateX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE DeserializeComputePipelineStateX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE GetGpuHardwareConfigurationX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE SetDebugErrorFilterX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreatePlacedRawShaderResourceViewX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreatePlacedRawUnorderedAccessViewX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE SetGpuMemoryPriorityX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreateCommandListX(const D3D12XBOX_COMMAND_LIST_DESC* pDesc, ID3D12CommandAllocator* pAllocator, ID3D12PipelineState* pInitialState, REFIID riid, void** ppCommandList) override;
	HRESULT STDMETHODCALLTYPE RegisterPagePoolX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE UnregisterPagePoolX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreateCommittedResourceX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreateComponentPlacedResourceX1(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE SetFrameIntervalX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE ScheduleFrameEventX(UINT Type, UINT IntervalOffsetUs, FrameObjList* pList, UINT Flags, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE WaitFrameEventX(UINT Type, UINT TimeOutInMs, void*  a3, UINT Flags, UINT64* pToken) override;
	HRESULT STDMETHODCALLTYPE GetFrameStatisticsX(UINT64 Token, UINT TypeSet, UINT* pCount, BYTE* pStatistics, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreateCounterSetX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreateCommittedOpaqueResourceX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreatePlacedOpaqueResourceX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE GetOpaqueResourceAllocationInfoX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreateFeedbackUnorderedAccessViewX(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) override;
	HRESULT STDMETHODCALLTYPE CreatePipelineLibrary(const void* pLibraryBlob, SIZE_T BlobLength, REFIID riid, void** ppPipelineLibrary) override;
	HRESULT STDMETHODCALLTYPE SetEventOnMultipleFenceCompletion(ID3D12Fence* const* ppFences, const UINT64* pFenceValues, UINT NumFences, D3D12_MULTIPLE_FENCE_WAIT_FLAGS Flags, HANDLE hEvent) override;
	HRESULT STDMETHODCALLTYPE SetResidencyPriority(UINT NumObjects, ID3D12Pageable* const* ppObjects, const D3D12_RESIDENCY_PRIORITY* pPriorities) override;
	HRESULT STDMETHODCALLTYPE CreatePipelineState(const D3D12_PIPELINE_STATE_STREAM_DESC* pDesc, REFIID riid, void** ppPipelineState) override;
	HRESULT STDMETHODCALLTYPE OpenExistingHeapFromAddress(const void* pAddress, REFIID riid, void** ppvHeap) override;
	HRESULT STDMETHODCALLTYPE OpenExistingHeapFromFileMapping(HANDLE hFileMapping, REFIID riid, void** ppvHeap) override;
	HRESULT STDMETHODCALLTYPE RegisterPagePool2X(void* a1, void* a2, void* a3, void* a4) override;
	HRESULT STDMETHODCALLTYPE EnqueueMakeResident(D3D12_RESIDENCY_FLAGS Flags, UINT NumObjects, ID3D12Pageable* const* ppObjects, ID3D12Fence* pFenceToSignal, UINT64 FenceValueToSignal) override;
};

struct SrvRec
{
	UINT dim;
	UINT fmt;
	void* res;
	UINT stride = 0;
	UINT nelem = 0;
	UINT first = 0;
};

enum
{
	D3D12XBOX_IAT_PADDING = 100,
	D3D12XBOX_IAT_PRIMITIVE_TOPOLOGY = 101,
	D3D12XBOX_IAT_DESCRIPTOR_TABLE = 102,
	D3D12XBOX_IAT_PIPELINE_STATE = 103,
	D3D12XBOX_IAT_DISPATCHX = 104,
	D3D12XBOX_IAT_DISPATCH_L2 = 105,
	D3D12XBOX_IAT_DPBB_BREAK_BATCH = 106,
	D3D12XBOX_IAT_DISPATCH_MESH_X = 107,
	D3D12XBOX_IAT_FLUSH_PIPELINEX = 108,
	D3D12XBOX_IAT_CS_LAUNCH_PARAMSX = 109,
};

struct ReservedRec
{
	void* res;
	UINT64 va;
	UINT64 bytes;
	UINT tiles;
};

struct VaHeap
{
	ID3D12Heap* heap = nullptr;
	UINT64 size = 0;
};

struct VaSeg
{
	UINT64 base = 0;
	ID3D12Heap* heap = nullptr;
};

enum
{
	D3D12XBOX_COMMAND_LIST_TYPE_DMA = 16
};

enum
{
	XBOX_PSO_SUBOBJ_FIRST = 23
};

EXTERN_C const GUID IID_ID3D12Resource = {

    0x696442be, 0xa72e, 0x4059, {0xbc, 0x79, 0x5b, 0x5c, 0x98, 0x04, 0x0f, 0xad}};

EXTERN_C const GUID IID_ID3D12Fence = {
    0x0a753dcf, 0xc4d8, 0x4b91, {0xad, 0xf6, 0xbe, 0x5a, 0x60, 0xd9, 0x5a, 0x76}};

EXTERN_C const GUID IID_ID3D12CommandAllocator = {
    0x6102dee4, 0xaf59, 0x4b09, {0xb9, 0x99, 0xb4, 0x4d, 0x73, 0xf0, 0x9b, 0x24}};

static std::vector<BufRec> GUploadBufs;
static SRWLOCK GBufLock = SRWLOCK_INIT;

static std::vector<GpuBufRange> GGpuBufs;
static SRWLOCK GGpuBufLock = SRWLOCK_INIT;

static std::map<UINT64, Shadow> GShadowPool;
static SRWLOCK GShadowLock = SRWLOCK_INIT;

static const LONG kShadowInFlight = 4;

static std::vector<std::pair<LONG, Shadow>> GShadowRetired;
static std::multimap<UINT, Shadow> GShadowFree;
static volatile LONG GShadowRetires = 0;
static std::vector<HeapRec> GCbvHeaps;
static SRWLOCK GHeapLock = SRWLOCK_INIT;

static std::map<void*, std::pair<SIZE_T, SIZE_T>> GHeapRanges;

static thread_local void* TBoundCbvHeapPtr = nullptr;

static std::map<SIZE_T, CbvRec> GCbvRecs;
static SRWLOCK GCbvLock = SRWLOCK_INIT;

static volatile LONG GPsoTotal = 0;
static volatile LONG GPsoCached = 0;
static volatile LONG GPsoPlaceholderCount = 0;
static volatile LONG GPsoBlendForced = 0;

static std::map<SIZE_T, CbvRec> GCbvBound;
static SRWLOCK GBoundLock = SRWLOCK_INIT;

static volatile LONG GCbvCreates = 0, GBindDecouples = 0;
static std::map<SIZE_T, SIZE_T> GDescCopy;
static SRWLOCK GCopyLock = SRWLOCK_INIT;

static std::map<void*, PlacedRec> GPlacedX;
static SRWLOCK GPlacedLock = SRWLOCK_INIT;

static std::vector<ID3D12Resource*> GPlacedPending;
static SRWLOCK GPendingLock = SRWLOCK_INIT;

EXTERN_C const GUID IID_ID3D12GraphicsCommandList;

static PFN_XGCreateTexture2DComputer xgCreate = nullptr;
static PFN_XGComputeOptimalTileMode xgTileMode = nullptr;
static bool xgTried = false;

thread_local uint32_t GCsParamStash[20] = {};
thread_local uint32_t GCsParamSize = 0;
static std::map<SIZE_T, SrvRec> GSrvRecs;
static SRWLOCK GSrvLock = SRWLOCK_INIT;

EXTERN_C const GUID WKPDID_D3DDebugObjectNameW = {

    0x4cca5fd8, 0x921f, 0x42c8, {0x85, 0x66, 0x70, 0xca, 0xf2, 0xa9, 0xb7, 0x41}};

static std::map<SIZE_T, ID3D12Resource*> GRtvRes;
static SRWLOCK GRtvLock = SRWLOCK_INIT;

static UINT GRtvInc = 0;
static std::map<SIZE_T, ID3D12Resource*> GDsvRes;
static SRWLOCK GDsvLock = SRWLOCK_INIT;

static const UINT kDesktopResourceFlagMask = 0x3F;

static std::vector<ReservedRec> GReserved;
static SRWLOCK GReservedLock = SRWLOCK_INIT;

static std::map<UINT64, ID3D12Heap*> GPoolHeaps;
static SRWLOCK GPoolLock = SRWLOCK_INIT;

static const UINT64 kXcPage = 64u * 1024u;

static std::map<UINT64, VaHeap> GVaHeaps;
static SRWLOCK GVaHeapLock = SRWLOCK_INIT;

static const UINT64 kVaSegShift = 26;
static const UINT64 kVaSegSize = 1ull << kVaSegShift;

static std::map<UINT64, VaSeg> GVaSegs;
static SRWLOCK GVaSegLock = SRWLOCK_INIT;

static int GHeapTier = 0;
EXTERN_C const GUID IID_ID3D12GraphicsCommandList;
static SRWLOCK GFrameEvLock = SRWLOCK_INIT;

static HANDLE GFrameEv[32];
static UINT GFrameEvCount = 0;
static HANDLE GFramePacer = nullptr;
static std::map<void*, std::string> GPsoKeys;
static SRWLOCK GPsoKeyLock = SRWLOCK_INIT;

static std::map<void*, std::vector<BYTE>> GPsoStreams;
static SRWLOCK GPsoStreamLock = SRWLOCK_INIT;

static volatile LONG64 GPsoCreateTicks = 0, GPsoCreateMaxTicks = 0;
static volatile LONG GPsoCreateCalls = 0;
static volatile LONG64 GPsoRewriteTicks = 0, GPsoRewriteMaxTicks = 0;
static volatile LONG GPsoRewriteCalls = 0;

static LONG64 NowTicks()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

static double TicksToMs(LONG64 ticks)
{
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	return freq.QuadPart ? (double)ticks * 1000.0 / (double)freq.QuadPart : 0.0;
}

static void NoteTicks(volatile LONG64* total, volatile LONG* calls, volatile LONG64* max, LONG64 delta)
{
	InterlockedAdd64(total, delta);
	if (calls)
	{
		InterlockedIncrement(calls);
	}
	for (;;)
	{
		LONG64 seen = *max;
		if (delta <= seen || InterlockedCompareExchange64(max, delta, seen) == seen)
		{
			break;
		}
	}
}

static volatile LONG GPsoAllCreates = 0;
static volatile LONG64 GLastStatsTicks = 0;

static void MaybeLogPsoStats()
{
	LONG n = InterlockedIncrement(&GPsoAllCreates);
	LONG64 now = NowTicks();
	LONG64 last = GLastStatsTicks;
	bool due = (n % 25) == 0 || last == 0 || TicksToMs(now - last) >= 10000.0;
	if (!due || InterlockedCompareExchange64(&GLastStatsTicks, now, last) != last)
	{
		return;
	}

	GDKScarlett::D3D12X::RecompilerTimings t{};
	GDKScarlett::D3D12X::GetRecompilerTimings(&t);
	LOGF("pso-stats: creates=%ld total=%ld cached=%ld placeholder=%ld blendForced=%ld "
	     "| decode=%.0fms/%ld (memo %ld alias %ld) cacheIO=%.0fms/%ld fxc=%.0fms/%ld "
	     "scan=%ld tries/%lld instrs",
	     n, GPsoTotal, GPsoCached, GPsoPlaceholderCount, GPsoBlendForced,
	     t.locateMs, t.locateCalls, t.locateMemoHits, t.aliasHits,
	     t.cacheIoMs, t.cacheIoCalls, t.compileMs, t.compileCalls,
	     t.scanAttempts, t.scanInstructions);
	LOGF("pso-timing: driverPSO=%.0fms/%ld (max %.0fms) rewrite=%.0fms/%ld (max %.0fms) "
	     "translate=%.0fms/%ld linkfix=%.0fms/%ld aliasIO=%.0fms/%ld",
	     TicksToMs(GPsoCreateTicks), GPsoCreateCalls, TicksToMs(GPsoCreateMaxTicks),
	     TicksToMs(GPsoRewriteTicks), GPsoRewriteCalls, TicksToMs(GPsoRewriteMaxTicks),
	     t.translateMs, t.translateCalls, t.linkFixMs, t.linkFixCalls,
	     t.aliasIoMs, t.aliasIoCalls);
	LOGF("pso-timing: worst single op - decode=%.0fms translate=%.0fms fxc=%.0fms",
	     t.maxLocateMs, t.maxTranslateMs, t.maxCompileMs);
	GDKScarlett::D3D12X::LogRecompilerStats();
}

static HRESULT TimedCreatePipelineState(ID3D12Device2* device,
                                        const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid,
                                        void** ppPipelineState)
{
	LONG64 t0 = NowTicks();
	HRESULT hr = device->CreatePipelineState(desc, riid, ppPipelineState);
	NoteTicks(&GPsoCreateTicks, &GPsoCreateCalls, &GPsoCreateMaxTicks, NowTicks() - t0);
	MaybeLogPsoStats();
	return hr;
}

static HRESULT TimedCreateComputePipelineState(ID3D12Device* device,
                                               const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,
                                               REFIID riid, void** ppPipelineState)
{
	LONG64 t0 = NowTicks();
	HRESULT hr = device->CreateComputePipelineState(desc, riid, ppPipelineState);
	NoteTicks(&GPsoCreateTicks, &GPsoCreateCalls, &GPsoCreateMaxTicks, NowTicks() - t0);
	MaybeLogPsoStats();
	return hr;
}

static HRESULT TimedCreateGraphicsPipelineState(ID3D12Device* device,
                                                const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
                                                REFIID riid, void** ppPipelineState)
{
	LONG64 t0 = NowTicks();
	HRESULT hr = device->CreateGraphicsPipelineState(desc, riid, ppPipelineState);
	NoteTicks(&GPsoCreateTicks, &GPsoCreateCalls, &GPsoCreateMaxTicks, NowTicks() - t0);
	MaybeLogPsoStats();
	return hr;
}

static std::set<void*> GPsoPlaceholder;
static const SIZE_T kDerivedDescStride = 332;

static void RegisterPsoKeys(void* pso, const char* keys);
static bool SrvAtHandle(SIZE_T stagingPtr, UINT* dim, UINT* fmt, void** res);
static void PurgeRecordsInRange(SIZE_T lo, SIZE_T hi);
static void BufResName(void* res, char* out, size_t cap);
static void InvalidateSrvRec(SIZE_T dst);
static bool SrvRecAtHandle(SIZE_T stagingPtr, SrvRec* out);

static void RecordUploadBuffer(ID3D12Resource* res)
{
	if (!res)
		return;
	D3D12_RESOURCE_DESC rd = res->GetDesc();
	if (rd.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
		return;
	void* cpu = nullptr;
	if (FAILED(res->Map(0, nullptr, &cpu)) || !cpu)
		return;
	D3D12_GPU_VIRTUAL_ADDRESS va = res->GetGPUVirtualAddress();
	if (!va)
		return;
	AcquireSRWLockExclusive(&GBufLock);
	GUploadBufs.push_back({va, rd.Width, (uint8_t*)cpu});
	ReleaseSRWLockExclusive(&GBufLock);
}

static void RecordGpuBuffer(ID3D12Resource* res)
{
	if (!res)
		return;
	D3D12_RESOURCE_DESC rd = res->GetDesc();
	if (rd.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
		return;
	D3D12_GPU_VIRTUAL_ADDRESS va = res->GetGPUVirtualAddress();
	if (!va)
		return;
	AcquireSRWLockExclusive(&GGpuBufLock);
	GGpuBufs.push_back({va, rd.Width, (void*)res});
	ReleaseSRWLockExclusive(&GGpuBufLock);
}

EXTERN_C void* BufForGpuVA(UINT64 va, UINT64* off)
{
	void* r = nullptr;
	AcquireSRWLockShared(&GGpuBufLock);
	for (auto& b : GGpuBufs)
		if (va >= b.va && va < b.va + b.size)
		{
			r = b.res;
			if (off)
				*off = va - b.va;
			break;
		}
	ReleaseSRWLockShared(&GGpuBufLock);
	return r;
}

static uint8_t* CpuForVA(D3D12_GPU_VIRTUAL_ADDRESS va)
{
	uint8_t* r = nullptr;
	AcquireSRWLockShared(&GBufLock);
	for (auto& b : GUploadBufs)
		if (va >= b.va && va < b.va + b.size)
		{
			r = b.cpu + (va - b.va);
			break;
		}
	ReleaseSRWLockShared(&GBufLock);
	return r;
}

static void RegisterCbvHeap(UINT64 gpuBase, SIZE_T cpuBase, UINT inc, UINT num, void* heap)
{
	AcquireSRWLockExclusive(&GHeapLock);
	SIZE_T end = cpuBase + (SIZE_T)((UINT64)inc * num);
	for (size_t i = GCbvHeaps.size(); i-- > 0;)
	{
		const HeapRec& h = GCbvHeaps[i];
		SIZE_T hEnd = h.cpuBase + (SIZE_T)((UINT64)h.inc * h.num);
		if (h.heap == heap || (h.cpuBase < end && cpuBase < hEnd))
			GCbvHeaps.erase(GCbvHeaps.begin() + i);
	}
	GCbvHeaps.push_back({gpuBase, cpuBase, inc, num, heap});
	ReleaseSRWLockExclusive(&GHeapLock);
}

void GDKScarlett::D3D12X::UnregisterCbvHeap(void* realHeap)
{
	SIZE_T lo = 0, hi = 0;
	AcquireSRWLockExclusive(&GHeapLock);
	for (size_t i = GCbvHeaps.size(); i-- > 0;)
		if (GCbvHeaps[i].heap == realHeap)
			GCbvHeaps.erase(GCbvHeaps.begin() + i);
	auto rit = GHeapRanges.find(realHeap);
	if (rit != GHeapRanges.end())
	{
		lo = rit->second.first;
		hi = rit->second.second;
		GHeapRanges.erase(rit);
	}
	ReleaseSRWLockExclusive(&GHeapLock);
	if (lo < hi)
		PurgeRecordsInRange(lo, hi);
}

EXTERN_C void RegisterHeapRange(void* realHeap, SIZE_T cpuBase, SIZE_T bytes)
{
	AcquireSRWLockExclusive(&GHeapLock);
	GHeapRanges[realHeap] = {cpuBase, cpuBase + bytes};
	ReleaseSRWLockExclusive(&GHeapLock);
}

void GDKScarlett::D3D12X::NoteBoundCbvHeap(void* realHeap)
{
	TBoundCbvHeapPtr = realHeap;
}

static bool BoundCbvHeap(HeapRec* out)
{
	if (!TBoundCbvHeapPtr)
		return false;
	bool ok = false;
	AcquireSRWLockShared(&GHeapLock);
	for (auto& h : GCbvHeaps)
		if (h.heap == TBoundCbvHeapPtr)
		{
			*out = h;
			ok = true;
			break;
		}
	ReleaseSRWLockShared(&GHeapLock);
	return ok;
}

static void RecordDescCopy(SIZE_T dst, SIZE_T src)
{
	AcquireSRWLockExclusive(&GCopyLock);
	GDescCopy[dst] = src;
	ReleaseSRWLockExclusive(&GCopyLock);
	{
		CbvRec rec{};
		bool have = false;
		AcquireSRWLockShared(&GCbvLock);
		auto it = GCbvRecs.find(src);
		if (it != GCbvRecs.end())
		{
			rec = it->second;
			have = true;
		}
		ReleaseSRWLockShared(&GCbvLock);
		AcquireSRWLockExclusive(&GBoundLock);
		if (have)
			GCbvBound[dst] = rec;
		else
			GCbvBound.erase(dst);
		ReleaseSRWLockExclusive(&GBoundLock);
	}
}

static void InvalidateDescCopy(SIZE_T dst)
{
	AcquireSRWLockExclusive(&GCopyLock);
	GDescCopy.erase(dst);
	ReleaseSRWLockExclusive(&GCopyLock);
}

static SIZE_T ResolveDescCopy(SIZE_T dst)
{
	SIZE_T r = dst;
	AcquireSRWLockShared(&GCopyLock);
	auto it = GDescCopy.find(dst);
	if (it != GDescCopy.end())
		r = it->second;
	ReleaseSRWLockShared(&GCopyLock);
	return r;
}

static void RecordPlacedX(void* res, UINT64 va, const D3D12_RESOURCE_DESC* d, UINT state)
{
	if (!res || !d)
		return;
	((IUnknown*)res)->AddRef();
	std::vector<IUnknown*> evicted;
	AcquireSRWLockExclusive(&GPlacedLock);
	for (auto it = GPlacedX.begin(); it != GPlacedX.end();)
	{
		if (it->second.va == va && it->first != res)
		{
			evicted.push_back((IUnknown*)it->first);
			it = GPlacedX.erase(it);
		}
		else
		{
			++it;
		}
	}
	GPlacedX[res] = {va, d->Width, (UINT)d->Height, (UINT)d->Format, (UINT)d->Dimension, state, 0};
	ReleaseSRWLockExclusive(&GPlacedLock);
	for (IUnknown* u : evicted)
		u->Release();
}

EXTERN_C bool PlacedForRes(void* res, PlacedRec* out)
{
	bool r = false;
	AcquireSRWLockShared(&GPlacedLock);
	auto it = GPlacedX.find(res);
	if (it != GPlacedX.end())
	{
		if (out)
			*out = it->second;
		r = true;
	}
	ReleaseSRWLockShared(&GPlacedLock);
	return r;
}

static bool ModuleDirFile(char* path, size_t n, const char*)
{
	HMODULE module = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   (LPCSTR)&ModuleDirFile, &module);
	return GetModuleFileNameA(module, path, (DWORD)n) != 0;
}

void GDKScarlett::D3D12X::QueuePlacedUpload(void* resv)
{
	ID3D12Resource* res = (ID3D12Resource*)resv;
	PlacedRec rec{};
	if (!PlacedForRes(res, &rec) || !rec.va)
		return;
	if (rec.dim != 3 )
		return;
	{
		D3D12_RESOURCE_DESC rd = res->GetDesc();
		if (rd.Flags &
		    (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
			return;
		if (rec.dim == 1 && (rd.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
			return;
	}
	UINT64 now = GetTickCount64();
	if (rec.lastUpload && now - rec.lastUpload < 1000)
		return;
	AcquireSRWLockExclusive(&GPendingLock);
	bool dup = false;
	for (auto* p : GPlacedPending)
		if (p == res)
		{
			dup = true;
			break;
		}
	if (!dup)
	{
		res->AddRef();
		GPlacedPending.push_back(res);
	}
	ReleaseSRWLockExclusive(&GPendingLock);
}

static bool FmtBlockInfo(UINT fmt, UINT& blockBytes, UINT& blockDim)
{
	switch (fmt)
	{
	case 70:
	case 71:
	case 72:
		blockBytes = 8;
		blockDim = 4;
		return true;
	case 73:
	case 74:
	case 75:
		blockBytes = 16;
		blockDim = 4;
		return true;
	case 76:
	case 77:
	case 78:
		blockBytes = 16;
		blockDim = 4;
		return true;
	case 79:
	case 80:
	case 81:
		blockBytes = 8;
		blockDim = 4;
		return true;
	case 82:
	case 83:
	case 84:
		blockBytes = 16;
		blockDim = 4;
		return true;
	case 94:
	case 95:
	case 96:
		blockBytes = 16;
		blockDim = 4;
		return true;
	case 97:
	case 98:
	case 99:
		blockBytes = 16;
		blockDim = 4;
		return true;
	case 28:
	case 29:
	case 30:
		blockBytes = 4;
		blockDim = 1;
		return true;
	case 87:
	case 88:
	case 89:
	case 90:
	case 91:
	case 92:
	case 93:
		blockBytes = 4;
		blockDim = 1;
		return true;
	case 61:
	case 65:
		blockBytes = 1;
		blockDim = 1;
		return true;
	case 10:
	case 11:
	case 12:
	case 13:
		blockBytes = 8;
		blockDim = 1;
		return true;
	case 24:
	case 26:
		blockBytes = 4;
		blockDim = 1;
		return true;
	case 34:
	case 35:
	case 36:
	case 37:
		blockBytes = 4;
		blockDim = 1;
		return true;
	case 41:
	case 42:
	case 43:
		blockBytes = 4;
		blockDim = 1;
		return true;
	case 16:
	case 17:
	case 18:
		blockBytes = 8;
		blockDim = 1;
		return true;
	case 2:
		blockBytes = 16;
		blockDim = 1;
		return true;
	default:
		return false;
	}
}

static void XgInit()
{
	if (xgTried)
		return;
	xgTried = true;
	HMODULE h = LoadLibraryA("xg_x.dll");
	if (!h)
	{
		LOGF("placedX-sync: xg_x.dll NOT loaded - detile disabled");
		return;
	}
	xgCreate = (PFN_XGCreateTexture2DComputer)GetProcAddress(h, "XGCreateTexture2DComputer");
	xgTileMode = (PFN_XGComputeOptimalTileMode)GetProcAddress(h, "XGComputeOptimalTileMode");
}

static bool DetileToLinear(UINT64 va, UINT fmt, UINT w, UINT h, void* dst, UINT dstRowPitch,
                             UINT dstRows)
{
	XgInit();
	if (!xgCreate || !xgTileMode || !va || !dst)
		return false;
	XG_TEXTURE2D_DESC_x d{};
	d.Width = w;
	d.Height = h;
	d.MipLevels = 1;
	d.ArraySize = 1;
	d.Format = fmt;
	d.SampleDesc.Count = 1;
	d.SampleDesc.Quality = 0;
	d.Usage = 0 ;
	d.BindFlags = 0x8 ;
	XGComputer* comp = nullptr;
	bool ok = false;
	__try
	{
		d.TileMode = xgTileMode(3 , fmt, w, h, 1, 1, 0x8 , 0);
		if (FAILED(xgCreate(&d, &comp)) || !comp)
			return false;
		UINT64 srcSize = comp->vtbl->GetResourceSizeBytes(comp);
		MEMORY_BASIC_INFORMATION mbi{};
		if (srcSize && VirtualQuery((LPCVOID)va, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
		    !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
		{
			UINT64 avail =
			    (UINT64)((const uint8_t*)mbi.BaseAddress + mbi.RegionSize - (const uint8_t*)va);
			if (avail >= srcSize)
			{
				UINT slice = dstRowPitch * dstRows;
				ok = SUCCEEDED(comp->vtbl->CopyFromSubresource(comp, dst, 0, 0, (const void*)va,
				                                               dstRowPitch, slice));
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		ok = false;
	}
	if (comp)
		comp->vtbl->Release(comp);
	return ok;
}

void GDKScarlett::D3D12X::FlushPlacedUploads(ID3D12CommandQueue* queue)
{
	if (!queue)
		return;
	std::vector<ID3D12Resource*> work;
	AcquireSRWLockExclusive(&GPendingLock);
	work.swap(GPlacedPending);
	ReleaseSRWLockExclusive(&GPendingLock);
	if (work.empty())
		return;

	auto releaseAll = [&work]
	{
		for (auto* r : work)
			if (r)
				r->Release();
	};

	ID3D12Device* dev = nullptr;
	if (FAILED(work[0]->GetDevice(IID_ID3D12Device, (void**)&dev)) || !dev)
	{
		releaseAll();
		return;
	}
	struct DevRelease
	{
		ID3D12Device* d;
		~DevRelease() { d->Release(); }
	} devRelease{dev};

	static ID3D12CommandAllocator* allocator = nullptr;
	static ID3D12GraphicsCommandList* commandList = nullptr;
	static ID3D12Fence* fence = nullptr;
	static UINT64 fenceValue = 0;
	static HANDLE fenceEvent = nullptr;
	static std::vector<ID3D12Resource*> keepAlive;
	if (!allocator &&
	    FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
	                                       IID_ID3D12CommandAllocator, (void**)&allocator)))
	{
		releaseAll();
		return;
	}
	if (!commandList)
	{
		if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
		                                  IID_ID3D12GraphicsCommandList, (void**)&commandList)))
		{
			releaseAll();
			return;
		}
		commandList->Close();
	}
	if (!fence)
	{
		if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence, (void**)&fence)))
		{
			releaseAll();
			return;
		}
		fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	}
	if (fenceValue && fence->GetCompletedValue() < fenceValue && fenceEvent)
	{
		fence->SetEventOnCompletion(fenceValue, fenceEvent);
		if (WaitForSingleObject(fenceEvent, 2000) != WAIT_OBJECT_0)
		{
			static LONG stallCount = 0;
			LONG stalls = InterlockedIncrement(&stallCount);
			if (stalls <= 12)
			{
				LOGF("FlushPlacedUploads: upload fence stalled >2s, requeueing %zu resource(s) "
				     "(stall #%ld)",
				     work.size(), stalls);
			}
			AcquireSRWLockExclusive(&GPendingLock);
			GPlacedPending.insert(GPlacedPending.end(), work.begin(), work.end());
			ReleaseSRWLockExclusive(&GPendingLock);
			return;
		}
	}
	static std::vector<ID3D12Resource*> previousBatch;
	for (auto* r : previousBatch)
		if (r)
			r->Release();
	previousBatch.clear();
	for (auto* r : keepAlive)
		if (r)
			r->Release();
	keepAlive.clear();
	if (FAILED(allocator->Reset()) || FAILED(commandList->Reset(allocator, nullptr)))
	{
		releaseAll();
		return;
	}

	int recorded = 0;
	for (ID3D12Resource* res : work)
	{
		PlacedRec rec{};
		if (!PlacedForRes(res, &rec))
			continue;
		if (rec.dim == 1 )
		{
			MEMORY_BASIC_INFORMATION mbi{};
			if (!VirtualQuery((LPCVOID)rec.va, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
			    (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
				continue;
			SIZE_T avail =
			    (SIZE_T)((const uint8_t*)mbi.BaseAddress + mbi.RegionSize - (const uint8_t*)rec.va);
			SIZE_T n = (SIZE_T)(rec.width < (UINT64)avail ? rec.width : (UINT64)avail);
			if (!n)
				continue;
			D3D12_HEAP_PROPERTIES up{};
			up.Type = D3D12_HEAP_TYPE_UPLOAD;
			up.CreationNodeMask = 1;
			up.VisibleNodeMask = 1;
			D3D12_RESOURCE_DESC bd{};
			bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			bd.Width = n;
			bd.Height = 1;
			bd.DepthOrArraySize = 1;
			bd.MipLevels = 1;
			bd.SampleDesc.Count = 1;
			bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			ID3D12Resource* ub = nullptr;
			if (FAILED(dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd,
			                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			                                        IID_ID3D12Resource, (void**)&ub)))
				continue;
			uint8_t* dst = nullptr;
			if (FAILED(ub->Map(0, nullptr, (void**)&dst)) || !dst)
			{
				ub->Release();
				continue;
			}
			memcpy(dst, (const void*)rec.va, n);
			bool anyNz = false;
			for (SIZE_T i = 0; i < n && !anyNz; ++i)
				if (dst[i])
					anyNz = true;
			ub->Unmap(0, nullptr);
			keepAlive.push_back(ub);
			D3D12_RESOURCE_BARRIER b{};
			b.Transition.pResource = res;
			b.Transition.Subresource = 0;
			b.Transition.StateBefore = (D3D12_RESOURCE_STATES)rec.state;
			b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			bool needBarrier = (rec.state != (UINT)D3D12_RESOURCE_STATE_COPY_DEST &&
			                    rec.state != (UINT)D3D12_RESOURCE_STATE_COMMON);
			if (needBarrier)
				commandList->ResourceBarrier(1, &b);
			commandList->CopyBufferRegion(res, 0, ub, 0, n);
			if (needBarrier)
			{
				b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
				b.Transition.StateAfter = (D3D12_RESOURCE_STATES)rec.state;
				commandList->ResourceBarrier(1, &b);
			}
			++recorded;
			AcquireSRWLockExclusive(&GPlacedLock);
			{
				auto it = GPlacedX.find(res);
				if (it != GPlacedX.end())
					it->second.lastUpload = GetTickCount64();
			}
			ReleaseSRWLockExclusive(&GPlacedLock);
			continue;
		}
		UINT blockBytes = 0, blockDim = 0;
		if (!FmtBlockInfo(rec.fmt, blockBytes, blockDim))
		{
			continue;
		}
		UINT blocksW = ((UINT)rec.width + blockDim - 1) / blockDim;
		UINT blocksH = (rec.height + blockDim - 1) / blockDim;
		UINT srcPitch = blocksW * blockBytes;
		SIZE_T srcBytes = (SIZE_T)srcPitch * blocksH;

		MEMORY_BASIC_INFORMATION mbi{};
		if (!VirtualQuery((LPCVOID)rec.va, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
		    (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
			continue;
		SIZE_T avail =
		    (SIZE_T)((const uint8_t*)mbi.BaseAddress + mbi.RegionSize - (const uint8_t*)rec.va);
		if (avail < srcBytes)
			srcBytes = avail;

		D3D12_RESOURCE_DESC rd = res->GetDesc();
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
		UINT rows = 0;
		UINT64 rowBytes = 0, total = 0;
		dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowBytes, &total);

		D3D12_HEAP_PROPERTIES up{};
		up.Type = D3D12_HEAP_TYPE_UPLOAD;
		up.CreationNodeMask = 1;
		up.VisibleNodeMask = 1;
		D3D12_RESOURCE_DESC bd{};
		bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bd.Width = total;
		bd.Height = 1;
		bd.DepthOrArraySize = 1;
		bd.MipLevels = 1;
		bd.SampleDesc.Count = 1;
		bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ID3D12Resource* ub = nullptr;
		if (FAILED(dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd,
		                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		                                        IID_ID3D12Resource, (void**)&ub)))
			continue;
		uint8_t* dst = nullptr;
		if (FAILED(ub->Map(0, nullptr, (void**)&dst)) || !dst)
		{
			ub->Release();
			continue;
		}
		bool detiled = DetileToLinear(rec.va, rec.fmt, (UINT)rec.width, rec.height,
		                                dst + fp.Offset, fp.Footprint.RowPitch, rows);
		if (!detiled)
		{
			const uint8_t* src = (const uint8_t*)rec.va;
			for (UINT r = 0; r < rows; ++r)
			{
				SIZE_T off = (SIZE_T)r * srcPitch;
				if (off >= srcBytes)
					break;
				SIZE_T n = srcPitch;
				if (off + n > srcBytes)
					n = srcBytes - off;
				memcpy(dst + fp.Offset + (SIZE_T)r * fp.Footprint.RowPitch, src + off, n);
			}
		}
		ub->Unmap(0, nullptr);
		keepAlive.push_back(ub);

		D3D12_RESOURCE_BARRIER b{};
		b.Transition.pResource = res;
		b.Transition.Subresource = 0;
		b.Transition.StateBefore = (D3D12_RESOURCE_STATES)rec.state;
		b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		bool needBarrier = (rec.state != (UINT)D3D12_RESOURCE_STATE_COPY_DEST &&
		                    rec.state != (UINT)D3D12_RESOURCE_STATE_COMMON);
		if (needBarrier)
			commandList->ResourceBarrier(1, &b);
		D3D12_TEXTURE_COPY_LOCATION cd{}, cs{};
		cd.pResource = res;
		cd.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		cd.SubresourceIndex = 0;
		cs.pResource = ub;
		cs.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		cs.PlacedFootprint = fp;
		commandList->CopyTextureRegion(&cd, 0, 0, 0, &cs, nullptr);
		if (needBarrier)
		{
			b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			b.Transition.StateAfter = (D3D12_RESOURCE_STATES)rec.state;
			commandList->ResourceBarrier(1, &b);
		}
		++recorded;

		AcquireSRWLockExclusive(&GPlacedLock);
		auto it = GPlacedX.find(res);
		if (it != GPlacedX.end())
			it->second.lastUpload = GetTickCount64();
		ReleaseSRWLockExclusive(&GPlacedLock);

	}
	if (FAILED(commandList->Close()))
	{
		releaseAll();
		return;
	}
	if (recorded)
	{
		ID3D12CommandList* lists[] = {commandList};
		queue->ExecuteCommandLists(1, lists);
		queue->Signal(fence, ++fenceValue);
		previousBatch.insert(previousBatch.end(), work.begin(), work.end());
	}
	else
	{
		releaseAll();
	}
}

bool GDKScarlett::D3D12X::SrvForGpuHandle(UINT64 gpuHandlePtr, UINT* dim, UINT* fmt, void** res)
{
	SIZE_T cands[4];
	int nc = 0;
	HeapRec bh;
	if (BoundCbvHeap(&bh))
	{
		if (gpuHandlePtr >= bh.gpuBase && gpuHandlePtr < bh.gpuBase + (UINT64)bh.inc * bh.num)
			cands[nc++] = bh.cpuBase + (SIZE_T)(((gpuHandlePtr - bh.gpuBase) / bh.inc) * bh.inc);
	}
	AcquireSRWLockShared(&GHeapLock);
	for (auto& h : GCbvHeaps)
	{
		if (nc >= 4)
			break;
		if (gpuHandlePtr >= h.gpuBase && gpuHandlePtr < h.gpuBase + (UINT64)h.inc * h.num)
		{
			SIZE_T cpu = h.cpuBase + (SIZE_T)(((gpuHandlePtr - h.gpuBase) / h.inc) * h.inc);
			bool dup = false;
			for (int i = 0; i < nc; ++i)
				if (cands[i] == cpu)
					dup = true;
			if (!dup)
				cands[nc++] = cpu;
		}
	}
	ReleaseSRWLockShared(&GHeapLock);
	for (int i = 0; i < nc; ++i)
	{
		if (SrvAtHandle(cands[i], dim, fmt, res))
			return 1;
		SIZE_T src = ResolveDescCopy(cands[i]);
		if (src != cands[i] && SrvAtHandle(src, dim, fmt, res))
			return 1;
	}
	return 0;
}

void GDKScarlett::D3D12X::ShadowCopyForBind(UINT64 gpuHandlePtr, UINT rootParam, bool isCompute)
{
	static LONG callCount = 0;
	LONG call = InterlockedIncrement(&callCount);
	static LONG paramBindCount[16] = {};
	if (rootParam < 16)
		InterlockedIncrement(&paramBindCount[rootParam]);
	static LONG noHeapMatchCount = 0;
	struct Cand
	{
		SIZE_T cpu;
		UINT inc;
		SIZE_T end;
	};
	std::vector<Cand> cands;
	HeapRec bh;
	if (BoundCbvHeap(&bh))
	{
		if (gpuHandlePtr >= bh.gpuBase && gpuHandlePtr < bh.gpuBase + (UINT64)bh.inc * bh.num)
		{
			UINT64 idx = (gpuHandlePtr - bh.gpuBase) / bh.inc;
			cands.push_back({bh.cpuBase + (SIZE_T)(idx * bh.inc), bh.inc,
			                 bh.cpuBase + (SIZE_T)((UINT64)bh.inc * bh.num)});
		}
	}
	AcquireSRWLockShared(&GHeapLock);
	size_t nHeaps = GCbvHeaps.size();
	for (auto& h : GCbvHeaps)
	{
		if (gpuHandlePtr >= h.gpuBase && gpuHandlePtr < h.gpuBase + (UINT64)h.inc * h.num)
		{
			UINT64 idx = (gpuHandlePtr - h.gpuBase) / h.inc;
			Cand c = {h.cpuBase + (SIZE_T)(idx * h.inc), h.inc,
			          h.cpuBase + (SIZE_T)((UINT64)h.inc * h.num)};
			bool dup = false;
			for (auto& e : cands)
				if (e.cpu == c.cpu)
					dup = true;
			if (!dup)
				cands.push_back(c);
		}
	}
	ReleaseSRWLockShared(&GHeapLock);

	SIZE_T cpuPtr = 0;
	bool found = false;
	UINT heapInc = 0;
	SIZE_T heapEnd = 0;
	for (auto& c : cands)
	{
		SIZE_T staged = ResolveDescCopy(c.cpu);
		bool hit = false;
		AcquireSRWLockShared(&GCbvLock);
		hit = GCbvRecs.count(c.cpu) != 0 || GCbvRecs.count(staged) != 0;
		ReleaseSRWLockShared(&GCbvLock);
		if (hit)
		{
			cpuPtr = c.cpu;
			heapInc = c.inc;
			heapEnd = c.end;
			found = true;
			break;
		}
	}
	if (!found && !cands.empty())
	{
		cpuPtr = cands[0].cpu;
		heapInc = cands[0].inc;
		heapEnd = cands[0].end;
		found = true;
	}
	if (!found)
	{
		InterlockedIncrement(&noHeapMatchCount);
		return;
	}
	SIZE_T stagingPtr = ResolveDescCopy(cpuPtr);
	{
		UINT sdim = 0, sfmt = 0;
		void* sres = nullptr;
		if (SrvAtHandle(stagingPtr, &sdim, &sfmt, &sres) && sres)
			GDKScarlett::D3D12X::QueuePlacedUpload(sres);
	}
	const UINT kTableDescriptors = 16;
	size_t nRecs = 0, copied = 0;
	static LONG seenBig = 0, s_bPend = 0, s_bOwns = 0, s_bSrc = 0, s_bCopied = 0;
	for (UINT slot = 0; slot < kTableDescriptors && heapInc; ++slot)
	{
		SIZE_T slotCpu = cpuPtr + (SIZE_T)slot * heapInc;
		if (heapEnd && slotCpu >= heapEnd)
			break;
		CbvRec rec{};
		bool have = false;
		AcquireSRWLockShared(&GBoundLock);
		{
			auto bit = GCbvBound.find(slotCpu);
			if (bit != GCbvBound.end())
			{
				rec = bit->second;
				have = true;
			}
		}
		ReleaseSRWLockShared(&GBoundLock);
		SIZE_T slotStaging = ResolveDescCopy(slotCpu);
		CbvRec srec{};
		bool shave = false;
		AcquireSRWLockShared(&GCbvLock);
		nRecs = GCbvRecs.size();
		auto it = GCbvRecs.find(slotStaging);
		if (it == GCbvRecs.end())
			it = GCbvRecs.find(slotCpu);
		if (it != GCbvRecs.end())
		{
			srec = it->second;
			shave = true;
		}
		ReleaseSRWLockShared(&GCbvLock);
		if (have && shave && (rec.shadowCpu != srec.shadowCpu || rec.sourceVA != srec.sourceVA))
			InterlockedIncrement(&GBindDecouples);
		if (!have)
		{
			rec = srec;
			have = shave;
		}
		if (!have || !rec.shadowCpu)
			continue;
		const bool bigRec = rec.size >= 1024;
		if (bigRec)
			InterlockedIncrement(&seenBig);
		if (bigRec)
		{
			if (!rec.pending)
			{
				InterlockedIncrement(&s_bPend);
				continue;
			}
			LONG cur = GDKScarlett::D3D12X::PresentedFrameCount();
			if (InterlockedExchange(rec.pending, cur) == cur)
			{
				InterlockedIncrement(&s_bPend);
				continue;
			}
		}
		else if (!isCompute)
		{
			if (!rec.pending || InterlockedExchange(rec.pending, 0) == 0)
				continue;
		}
		else
		{
			if (rec.pending)
				InterlockedExchange(rec.pending, 0);
			uint8_t* p0 = CpuForVA(rec.sourceVA);
			if (p0 && slot == 0)
			{
				memcpy(GCsParamStash, p0, rec.size < 80 ? rec.size : 80);
				GCsParamSize = rec.size;
			}
		}
		bool owns = false;
		AcquireSRWLockShared(&GShadowLock);
		{
			auto sit = GShadowPool.find(rec.sourceVA);
			owns = (sit != GShadowPool.end() && sit->second.cpu == rec.shadowCpu);
		}
		ReleaseSRWLockShared(&GShadowLock);
		if (!owns)
		{
			if (bigRec)
				InterlockedIncrement(&s_bOwns);
			continue;
		}
		uint8_t* src = CpuForVA(rec.sourceVA);
		if (!src)
		{
			if (bigRec)
				InterlockedIncrement(&s_bSrc);
			continue;
		}
		memcpy(rec.shadowCpu, src, rec.size);
		if (bigRec)
			InterlockedIncrement(&s_bCopied);
		++copied;
	}
	static LONG bindTotal = 0, s_bindWithRecs = 0, s_slotsCopied = 0, s_bindNoCopy = 0;
	InterlockedIncrement(&bindTotal);
	if (nRecs)
		InterlockedIncrement(&s_bindWithRecs);
	if (copied)
		InterlockedAdd(&s_slotsCopied, (LONG)copied);
	else if (nRecs)
		InterlockedIncrement(&s_bindNoCopy);
	LONG tot = bindTotal;
	if ((tot % 2000) == 0)
	{
		AcquireSRWLockShared(&GBoundLock);
		size_t nBound = GCbvBound.size();
		ReleaseSRWLockShared(&GBoundLock);
		LOGF("cbv-stats: binds=%ld withRecs=%ld slotsCopied=%ld noCopy=%ld bound=%zu retires=%ld "
		     "| bigSeen=%ld bigCopied=%ld bigPend=%ld bigOwns=%ld bigSrc=%ld | creates=%ld decouple=%ld noHeap=%ld",
		     tot, s_bindWithRecs, s_slotsCopied, s_bindNoCopy, nBound, GShadowRetires,
		     seenBig, s_bCopied, s_bPend, s_bOwns, s_bSrc, GCbvCreates, GBindDecouples, noHeapMatchCount);
	}
}

HRESULT STDMETHODCALLTYPE XboxDevice::QueryInterface(REFIID riid, void** ppvObject)
{
	if (!ppvObject)
		return E_POINTER;
	if (riid == IID_IUnknown || riid == IID_ID3D12Device || riid == __uuidof(ID3D12Device1) ||
	    riid == __uuidof(ID3D12Device2) || riid == __uuidof(ID3D12Device3))
	{
		InterlockedIncrement(&refs);
		*ppvObject = this;
		return S_OK;
	}
	if (riid == IID_IDXGIDevice || riid == IID_IDXGIDevice1 || riid == IID_IDXGIDevice2)
	{
		if (!dxgiDevice)
			dxgiDevice = XboxDxgiDeviceCreate(real);
		HRESULT hr = dxgiDevice->QueryInterface(riid, ppvObject);
		return hr;
	}
	HRESULT hr = real->QueryInterface(riid, ppvObject);
	return hr;
}

ULONG STDMETHODCALLTYPE XboxDevice::AddRef()
{
	return InterlockedIncrement(&refs);
}

ULONG STDMETHODCALLTYPE XboxDevice::Release()
{
	LONG r = InterlockedDecrement(&refs);
	if (r == 0)
	{
		if (dxgiDevice)
			dxgiDevice->Release();
		real->Release();
		delete this;
	}
	return (ULONG)r;
}

HRESULT STDMETHODCALLTYPE XboxDevice::GetPrivateData(REFGUID guid, UINT* pDataSize,
                                                   void* pData)
{
	return real->GetPrivateData(guid, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE XboxDevice::SetPrivateData(REFGUID guid, UINT DataSize,
                                                   const void* pData)
{
	return real->SetPrivateData(guid, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE XboxDevice::SetPrivateDataInterface(REFGUID guid,
                                                            const IUnknown* pData)
{
	return real->SetPrivateDataInterface(guid, pData);
}

HRESULT STDMETHODCALLTYPE XboxDevice::SetName(LPCWSTR Name)
{
	return real->SetName(Name);
}

UINT STDMETHODCALLTYPE XboxDevice::GetNodeCount()
{
	return real->GetNodeCount();
}

static D3D12_COMMAND_LIST_TYPE TranslateListType(D3D12_COMMAND_LIST_TYPE t)
{
	if ((UINT)t == 16)
		return D3D12_COMMAND_LIST_TYPE_COPY;
	return t;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC* pDesc,
                                                       REFIID riid, void** ppCommandQueue)
{
	D3D12_COMMAND_QUEUE_DESC desc = {};
	if (pDesc)
	{
		desc = *pDesc;
		desc.Type = TranslateListType(desc.Type);
	}
	HRESULT hr = real->CreateCommandQueue(pDesc ? &desc : nullptr, riid, ppCommandQueue);
	if (SUCCEEDED(hr) && ppCommandQueue && *ppCommandQueue)
	{
		ID3D12CommandQueue* realQueue = (ID3D12CommandQueue*)*ppCommandQueue;
		*ppCommandQueue = XboxCommandQueueWrap(realQueue, (ID3D12Device*)this);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type,
                                                           REFIID riid, void** ppCommandAllocator)
{
	D3D12_COMMAND_LIST_TYPE t = TranslateListType(type);
	return real->CreateCommandAllocator(t, riid, ppCommandAllocator);
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc,
                               REFIID riid, void** ppPipelineState)
{
	if (pDesc)
	{
		const D3D12_RENDER_TARGET_BLEND_DESC& b = pDesc->BlendState.RenderTarget[0];
	}
	return TimedCreateGraphicsPipelineState(real, pDesc, riid, ppPipelineState);
}

static HRESULT CreateComputePSO(XboxDevice* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc,
                                  REFIID riid, void** ppPipelineState)
{
	if (!pDesc)
		return TimedCreateComputePipelineState(device->real, pDesc, riid, ppPipelineState);
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc = *pDesc;
	D3D12_SHADER_BYTECODE cs{};
	if (desc.CS.pShaderBytecode)
	{
		if (GDKScarlett::D3D12X::TryRecompileToDxil(6 , false, &desc.CS, &cs))
			desc.CS = cs;
		else if (GDKScarlett::D3D12X::TryRecompileToDxil(6, false, &desc.CS, &cs,
		                                                 false))
			desc.CS = cs;
	}
	HRESULT hr = TimedCreateComputePipelineState(device->real, &desc, riid, ppPipelineState);
	{
		const char* keys = GDKScarlett::D3D12X::PsoKeyCapture();
		RegisterPsoKeys(*ppPipelineState, (keys && *keys) ? keys : " (cs-placeholder)");
	}
	if (FAILED(hr))
	{
		D3D12_SHADER_BYTECODE ph{};
		if (GDKScarlett::D3D12X::TryRecompileToDxil(6, false, &pDesc->CS, &ph,
		                                            false))
		{
			desc = *pDesc;
			desc.CS = ph;
			HRESULT hr2 = TimedCreateComputePipelineState(device->real, &desc, riid, ppPipelineState);
			if (SUCCEEDED(hr2))
				hr = hr2;
		}
		if (FAILED(hr))
			LOGF("CreateComputePipelineState FAILED hr=0x%08X", (unsigned)hr);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc,
                              REFIID riid, void** ppPipelineState)
{
	return CreateComputePSO(this, pDesc, riid, ppPipelineState);
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateCommandList(UINT nodeMask,
                                                      D3D12_COMMAND_LIST_TYPE type,
                                                      ID3D12CommandAllocator* pCommandAllocator,
                                                      ID3D12PipelineState* pInitialState,
                                                      REFIID riid, void** ppCommandList)
{
	D3D12_COMMAND_LIST_TYPE t = TranslateListType(type);
	HRESULT hr = real->CreateCommandList(nodeMask, t, pCommandAllocator, pInitialState, riid,
	                                           ppCommandList);
	if (SUCCEEDED(hr) && ppCommandList && *ppCommandList)
	{
		ID3D12GraphicsCommandList* realList = (ID3D12GraphicsCommandList*)*ppCommandList;
		*ppCommandList = XboxCommandListWrap(realList, (ID3D12Device*)this);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CheckFeatureSupport(D3D12_FEATURE Feature,
                                                        void* pFeatureSupportData,
                                                        UINT FeatureSupportDataSize)
{
	return real->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc,
                        REFIID riid, void** ppvHeap)
{
	D3D12_DESCRIPTOR_HEAP_DESC clamped;
	const D3D12_DESCRIPTOR_HEAP_DESC* useDesc = pDescriptorHeapDesc;
	if (pDescriptorHeapDesc &&
	    (pDescriptorHeapDesc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) &&
	    pDescriptorHeapDesc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
	    pDescriptorHeapDesc->NumDescriptors > 1000000)
	{
		clamped = *pDescriptorHeapDesc;
		clamped.NumDescriptors = 1000000;
		useDesc = &clamped;
		LOGF("CreateDescriptorHeap: clamped NumDescriptors %u -> 1000000 (desktop limit)",
		     pDescriptorHeapDesc->NumDescriptors);
	}
	HRESULT hr = real->CreateDescriptorHeap(useDesc, riid, ppvHeap);

	if (SUCCEEDED(hr) && ppvHeap && *ppvHeap)
	{
		ID3D12DescriptorHeap* realHeap = (ID3D12DescriptorHeap*)*ppvHeap;
		if (pDescriptorHeapDesc &&
		    pDescriptorHeapDesc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
		    (pDescriptorHeapDesc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
		{
			UINT inc = real->GetDescriptorHandleIncrementSize(
			    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			UINT64 gpuBase = realHeap->GetGPUDescriptorHandleForHeapStart().ptr;
			SIZE_T cpuBase = realHeap->GetCPUDescriptorHandleForHeapStart().ptr;
			RegisterCbvHeap(gpuBase, cpuBase, inc, useDesc->NumDescriptors, realHeap);
		}
		if (pDescriptorHeapDesc &&
		    pDescriptorHeapDesc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
		{
			UINT inc2 = real->GetDescriptorHandleIncrementSize(
			    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			RegisterHeapRange(realHeap, realHeap->GetCPUDescriptorHandleForHeapStart().ptr,
			                    (SIZE_T)inc2 * useDesc->NumDescriptors);
		}
		*ppvHeap = XboxDescriptorHeapWrap(realHeap, (ID3D12Device*)this);
	}
	return hr;
}

UINT STDMETHODCALLTYPE XboxDevice::GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType)
{
	return real->GetDescriptorHandleIncrementSize(DescriptorHeapType);
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateRootSignature(UINT nodeMask,
                                                        const void* pBlobWithRootSignature,
                                                        SIZE_T blobLengthInBytes, REFIID riid,
                                                        void** ppvRootSignature)
{
	HRESULT hr = real->CreateRootSignature(nodeMask, pBlobWithRootSignature,
	                                             blobLengthInBytes, riid, ppvRootSignature);
	if (FAILED(hr))
		LOGF("CreateRootSignature FAILED hr=0x%08X (%llu bytes)", (unsigned)hr,
		     (unsigned long long)blobLengthInBytes);
	return hr;
}

void STDMETHODCALLTYPE XboxDevice::CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc,
                            D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
	InvalidateDescCopy(DestDescriptor.ptr);
	InvalidateSrvRec(DestDescriptor.ptr);
	if (!pDesc)
	{
		real->CreateConstantBufferView(pDesc, DestDescriptor);
		return;
	}
	D3D12_CONSTANT_BUFFER_VIEW_DESC d = *pDesc;

	{
		bool tracked = CpuForVA(d.BufferLocation) != nullptr;
		bool placed = false;
		UINT64 pva = 0;
		if (!tracked)
		{
			AcquireSRWLockShared(&GPlacedLock);
			for (auto& kv : GPlacedX)
			{
				if (kv.second.dim == (UINT)D3D12_RESOURCE_DIMENSION_BUFFER &&
				    d.BufferLocation >= kv.second.va &&
				    d.BufferLocation < kv.second.va + kv.second.width)
				{
					placed = true;
					pva = kv.second.va;
					break;
				}
			}
			ReleaseSRWLockShared(&GPlacedLock);
		}
		static LONG trackedCount = 0, s_ctPlaced = 0, s_ctUnknown = 0;
		InterlockedIncrement(tracked ? &trackedCount : placed ? &s_ctPlaced : &s_ctUnknown);
		static LONG bigCount = 0;
		LONG tot = trackedCount + s_ctPlaced + s_ctUnknown;
	}

	if (CpuForVA(d.BufferLocation) && d.SizeInBytes)
	{
		UINT aligned = (d.SizeInBytes + 255u) & ~255u;
		AcquireSRWLockExclusive(&GShadowLock);
		LONG now = GDKScarlett::D3D12X::PresentedFrameCount();
		for (size_t i = GShadowRetired.size(); i-- > 0;)
		{
			if (now - GShadowRetired[i].first >= kShadowInFlight || GShadowRetired.size() > 8192)
			{
				GShadowFree.insert({GShadowRetired[i].second.size, GShadowRetired[i].second});
				GShadowRetired.erase(GShadowRetired.begin() + i);
			}
		}
		auto it = GShadowPool.find(d.BufferLocation);
		Shadow sh = {nullptr, nullptr, 0, 0};
		if (it != GShadowPool.end())
		{
			GShadowRetired.push_back({now, it->second});
			GShadowPool.erase(it);
			InterlockedIncrement(&GShadowRetires);
		}
		auto fit = GShadowFree.lower_bound(aligned);
		if (fit != GShadowFree.end())
		{
			sh = fit->second;
			GShadowFree.erase(fit);
			GShadowPool[d.BufferLocation] = sh;
		}
		else
		{
			D3D12_HEAP_PROPERTIES hp = {};
			hp.Type = D3D12_HEAP_TYPE_UPLOAD;
			D3D12_RESOURCE_DESC rd = {};
			rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			rd.Width = aligned;
			rd.Height = 1;
			rd.DepthOrArraySize = 1;
			rd.MipLevels = 1;
			rd.SampleDesc.Count = 1;
			rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			ID3D12Resource* res = nullptr;
			if (SUCCEEDED(real->CreateCommittedResource(
			        &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			        IID_ID3D12Resource, (void**)&res)))
			{
				void* cpu = nullptr;
				if (SUCCEEDED(res->Map(0, nullptr, &cpu)) && cpu)
				{
					sh = {res, (uint8_t*)cpu, res->GetGPUVirtualAddress(), aligned};
					GShadowPool[d.BufferLocation] = sh;
				}
				else
				{
					res->Release();
				}
			}
		}
		ReleaseSRWLockExclusive(&GShadowLock);
		if (sh.cpu)
		{
			memcpy(sh.cpu, CpuForVA(d.BufferLocation), d.SizeInBytes);
			if (aligned > d.SizeInBytes)
				memset(sh.cpu + d.SizeInBytes, 0, aligned - d.SizeInBytes);
			D3D12_CONSTANT_BUFFER_VIEW_DESC sd = {sh.va, aligned};
			real->CreateConstantBufferView(&sd, DestDescriptor);
			InterlockedIncrement(&GCbvCreates);
			CbvRec rec = {d.BufferLocation, d.SizeInBytes, sh.cpu, new LONG(1)};
			AcquireSRWLockExclusive(&GCbvLock);
			GCbvRecs[DestDescriptor.ptr] = rec;
			ReleaseSRWLockExclusive(&GCbvLock);
			AcquireSRWLockExclusive(&GBoundLock);
			GCbvBound[DestDescriptor.ptr] = rec;
			ReleaseSRWLockExclusive(&GBoundLock);
			static LONG shCount = 0;
			if (InterlockedIncrement(&shCount) <= 4)
			{
				LOGF("cbv-shadow: registered (copy deferred to bind)");
			}
			return;
		}
	}

	const UINT64 kCbAlign = 256;
	UINT64 alignedBase = d.BufferLocation & ~(kCbAlign - 1);
	UINT delta = (UINT)(d.BufferLocation - alignedBase);
	if (delta)
	{
		UINT64 widened = (UINT64)d.SizeInBytes + delta;
		widened = (widened + kCbAlign - 1) & ~(kCbAlign - 1);
		if (widened > D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16ull)
			widened = D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16ull;
		d.BufferLocation = alignedBase;
		d.SizeInBytes = (UINT)widened;
	}

	if (d.SizeInBytes & (kCbAlign - 1))
	{
		UINT aligned = (UINT)((d.SizeInBytes + kCbAlign - 1) & ~(kCbAlign - 1));
		d.SizeInBytes = aligned;
	}
	real->CreateConstantBufferView(&d, DestDescriptor);
}

static void PurgeRecordsInRange(SIZE_T lo, SIZE_T hi)
{
	size_t nc = 0, ns = 0, nd = 0;
	AcquireSRWLockExclusive(&GCbvLock);
	for (auto it = GCbvRecs.lower_bound(lo); it != GCbvRecs.end() && it->first < hi; ++nc)
		it = GCbvRecs.erase(it);
	ReleaseSRWLockExclusive(&GCbvLock);
	AcquireSRWLockExclusive(&GSrvLock);
	for (auto it = GSrvRecs.lower_bound(lo); it != GSrvRecs.end() && it->first < hi; ++ns)
		it = GSrvRecs.erase(it);
	ReleaseSRWLockExclusive(&GSrvLock);
	AcquireSRWLockExclusive(&GCopyLock);
	for (auto it = GDescCopy.begin(); it != GDescCopy.end();)
	{
		bool dead = (it->first >= lo && it->first < hi) || (it->second >= lo && it->second < hi);
		if (dead)
		{
			it = GDescCopy.erase(it);
			++nd;
		}
		else
			++it;
	}
	ReleaseSRWLockExclusive(&GCopyLock);
}

static void InvalidateSrvRec(SIZE_T dst)
{
	AcquireSRWLockExclusive(&GSrvLock);
	GSrvRecs.erase(dst);
	ReleaseSRWLockExclusive(&GSrvLock);
}

void STDMETHODCALLTYPE XboxDevice::CreateShaderResourceView(ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc,
    D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
	real->CreateShaderResourceView(pResource, pDesc, DestDescriptor);
	InvalidateDescCopy(DestDescriptor.ptr);
	AcquireSRWLockExclusive(&GCbvLock);
	GCbvRecs.erase(DestDescriptor.ptr);
	ReleaseSRWLockExclusive(&GCbvLock);
	AcquireSRWLockExclusive(&GBoundLock);
	GCbvBound.erase(DestDescriptor.ptr);
	ReleaseSRWLockExclusive(&GBoundLock);
	UINT dim = pDesc ? (UINT)pDesc->ViewDimension : 0;
	UINT fmt = pDesc ? (UINT)pDesc->Format : 0;
	SrvRec rec{dim, fmt, (void*)pResource};
	if (pDesc && dim == 1)
	{
		rec.stride = pDesc->Buffer.StructureByteStride;
		rec.nelem = (UINT)pDesc->Buffer.NumElements;
		rec.first = (UINT)pDesc->Buffer.FirstElement;
	}
	AcquireSRWLockExclusive(&GSrvLock);
	GSrvRecs[DestDescriptor.ptr] = rec;
	ReleaseSRWLockExclusive(&GSrvLock);
}

static void BufResName(void* res, char* out, size_t cap)
{
	lstrcpynA(out, "<unnamed>", (int)cap);
	if (!res)
		return;
	WCHAR w[128];
	UINT n = sizeof(w);
	if (SUCCEEDED(((ID3D12Resource*)res)->GetPrivateData(WKPDID_D3DDebugObjectNameW, &n, w)) &&
	    n >= 2)
	{
		w[(n / 2) < 127 ? (n / 2) : 127] = 0;
		WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, nullptr, nullptr);
	}
}

static bool SrvRecAtHandle(SIZE_T stagingPtr, SrvRec* out)
{
	bool r = false;
	AcquireSRWLockShared(&GSrvLock);
	auto it = GSrvRecs.find(stagingPtr);
	if (it != GSrvRecs.end())
	{
		if (out)
			*out = it->second;
		r = true;
	}
	ReleaseSRWLockShared(&GSrvLock);
	return r;
}

static bool SrvAtHandle(SIZE_T stagingPtr, UINT* dim, UINT* fmt, void** res)
{
	bool r = false;
	AcquireSRWLockShared(&GSrvLock);
	auto it = GSrvRecs.find(stagingPtr);
	if (it != GSrvRecs.end())
	{
		if (dim)
			*dim = it->second.dim;
		if (fmt)
			*fmt = it->second.fmt;
		if (res)
			*res = it->second.res;
		r = true;
	}
	ReleaseSRWLockShared(&GSrvLock);
	return r;
}

void STDMETHODCALLTYPE XboxDevice::CreateUnorderedAccessView(ID3D12Resource* pResource, ID3D12Resource* pCounterResource,
    const D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
	real->CreateUnorderedAccessView(pResource, pCounterResource, pDesc, DestDescriptor);
	InvalidateDescCopy(DestDescriptor.ptr);
	InvalidateSrvRec(DestDescriptor.ptr);
	AcquireSRWLockExclusive(&GBoundLock);
	GCbvBound.erase(DestDescriptor.ptr);
	ReleaseSRWLockExclusive(&GBoundLock);
	AcquireSRWLockExclusive(&GCbvLock);
	GCbvRecs.erase(DestDescriptor.ptr);
	ReleaseSRWLockExclusive(&GCbvLock);
}

void STDMETHODCALLTYPE XboxDevice::CreateRenderTargetView(ID3D12Resource* pResource,
                                                        const D3D12_RENDER_TARGET_VIEW_DESC* pDesc,
                                                        D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
	if (!GRtvInc)
		GRtvInc = real->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	if (DestDescriptor.ptr)
	{
		AcquireSRWLockExclusive(&GRtvLock);
		GRtvRes[DestDescriptor.ptr] = pResource;
		ReleaseSRWLockExclusive(&GRtvLock);
	}
	real->CreateRenderTargetView(pResource, pDesc, DestDescriptor);
}

void STDMETHODCALLTYPE XboxDevice::CreateDepthStencilView(ID3D12Resource* pResource,
                                                        const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc,
                                                        D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
	if (DestDescriptor.ptr)
	{
		AcquireSRWLockExclusive(&GDsvLock);
		GDsvRes[DestDescriptor.ptr] = pResource;
		ReleaseSRWLockExclusive(&GDsvLock);
	}
	real->CreateDepthStencilView(pResource, pDesc, DestDescriptor);
}

void STDMETHODCALLTYPE XboxDevice::CreateSampler(const D3D12_SAMPLER_DESC* pDesc,
                                               D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
	real->CreateSampler(pDesc, DestDescriptor);
}

void STDMETHODCALLTYPE XboxDevice::CopyDescriptors(UINT NumDestDescriptorRanges,
    const D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts,
    const UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges,
    const D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts,
    const UINT* pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType)
{
	real->CopyDescriptors(NumDestDescriptorRanges, pDestDescriptorRangeStarts,
	                            pDestDescriptorRangeSizes, NumSrcDescriptorRanges,
	                            pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes,
	                            DescriptorHeapsType);
	if (DescriptorHeapsType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
	{
		UINT inc = real->GetDescriptorHandleIncrementSize(DescriptorHeapsType);
		UINT di = 0, dOff = 0, si = 0, sOff = 0;
		while (di < NumDestDescriptorRanges && si < NumSrcDescriptorRanges)
		{
			UINT dsz = pDestDescriptorRangeSizes ? pDestDescriptorRangeSizes[di] : 1;
			UINT ssz = pSrcDescriptorRangeSizes ? pSrcDescriptorRangeSizes[si] : 1;
			RecordDescCopy(pDestDescriptorRangeStarts[di].ptr + (SIZE_T)dOff * inc,
			                 pSrcDescriptorRangeStarts[si].ptr + (SIZE_T)sOff * inc);
			if (++dOff >= dsz)
			{
				dOff = 0;
				++di;
			}
			if (++sOff >= ssz)
			{
				sOff = 0;
				++si;
			}
		}
	}
}

void STDMETHODCALLTYPE XboxDevice::CopyDescriptorsSimple(UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
    D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
    D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType)
{
	real->CopyDescriptorsSimple(NumDescriptors, DestDescriptorRangeStart,
	                                  SrcDescriptorRangeStart, DescriptorHeapsType);
	if (DescriptorHeapsType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
	{
		UINT inc = real->GetDescriptorHandleIncrementSize(DescriptorHeapsType);
		for (UINT i = 0; i < NumDescriptors; ++i)
			RecordDescCopy(DestDescriptorRangeStart.ptr + (SIZE_T)i * inc,
			                 SrcDescriptorRangeStart.ptr + (SIZE_T)i * inc);
	}
}

static D3D12_RESOURCE_DESC SanitizeResourceDesc(const D3D12_RESOURCE_DESC* pDesc, const char* who)
{
	D3D12_RESOURCE_DESC d = *pDesc;
	UINT stripped = (UINT)d.Flags & ~kDesktopResourceFlagMask;
	if (stripped)
	{
		d.Flags = (D3D12_RESOURCE_FLAGS)((UINT)d.Flags & kDesktopResourceFlagMask);
	}
	if ((UINT)d.Layout > 3)
	{
		d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	}
	if (d.Alignment != 0 && d.Alignment != 4096 && d.Alignment != 65536 && d.Alignment != 4194304)
	{
		d.Alignment = 0;
	}
	return d;
}

static D3D12_RESOURCE_STATES SanitizeState(D3D12_RESOURCE_STATES s, const char* who)
{
	const UINT kXboxStateBits = 0xFFC00000u;
	UINT stripped = (UINT)s & kXboxStateBits;
	if (stripped)
	{
		s = (D3D12_RESOURCE_STATES)((UINT)s & ~kXboxStateBits);
	}
	return s;
}

static D3D12_HEAP_FLAGS SanitizeHeapFlags(D3D12_HEAP_FLAGS f, const char* who)
{
	if ((UINT)f & 0x2u)
	{
		f = (D3D12_HEAP_FLAGS)((UINT)f & ~0x2u);
	}
	return f;
}

UINT64 GDKScarlett::D3D12X::MarkInfoQueue(ID3D12Device* real)
{
	if (!real)
		return 0;
	ID3D12InfoQueue* iq = nullptr;
	if (FAILED(real->QueryInterface(__uuidof(ID3D12InfoQueue), (void**)&iq)) || !iq)
		return 0;
	UINT64 n = iq->GetNumStoredMessages();
	iq->Release();
	return n;
}

void GDKScarlett::D3D12X::DrainInfoQueue(ID3D12Device* real, const char* who, UINT64 from)
{
	if (!real)
		return;
	ID3D12InfoQueue* iq = nullptr;
	if (FAILED(real->QueryInterface(__uuidof(ID3D12InfoQueue), (void**)&iq)) || !iq)
		return;
	UINT64 n = iq->GetNumStoredMessages();
	if (from > n)
		from = 0;
	for (UINT64 i = from; i < n; ++i)
	{
		SIZE_T len = 0;
		if (FAILED(iq->GetMessage(i, nullptr, &len)) || len == 0)
			continue;
		D3D12_MESSAGE* msg = (D3D12_MESSAGE*)HeapAlloc(GetProcessHeap(), 0, len);
		if (!msg)
			break;
		if (SUCCEEDED(iq->GetMessage(i, msg, &len)))
		{
			const char* sev = "INFO";
			switch (msg->Severity)
			{
			case D3D12_MESSAGE_SEVERITY_CORRUPTION:
				sev = "CORRUPTION";
				break;
			case D3D12_MESSAGE_SEVERITY_ERROR:
				sev = "ERROR";
				break;
			case D3D12_MESSAGE_SEVERITY_WARNING:
				sev = "WARNING";
				break;
			default:
				break;
			}
			LOGF("D3D12 %s [%s id=%d]: %.*s", sev, who, (int)msg->ID,
			     (int)msg->DescriptionByteLength, msg->pDescription);
		}
		HeapFree(GetProcessHeap(), 0, msg);
	}
	if (n)
		iq->ClearStoredMessages();
	iq->Release();
}

void GDKScarlett::D3D12X::ReportDeviceRemoved(ID3D12Device* real, const char* who)
{
	if (!real)
		return;
	GDKScarlett::D3D12X::DrainInfoQueue(real, who);
	HRESULT reason = real->GetDeviceRemovedReason();
	LOGF("*** DEVICE REMOVED during %s - GetDeviceRemovedReason = 0x%08X", who, (unsigned)reason);

	ID3D12DeviceRemovedExtendedData* dred = nullptr;
	if (FAILED(real->QueryInterface(__uuidof(ID3D12DeviceRemovedExtendedData), (void**)&dred)) ||
	    !dred)
	{
		LOGF("    (DRED not available on this device)");
		return;
	}
	D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT bc = {};
	if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&bc)))
	{
		for (const D3D12_AUTO_BREADCRUMB_NODE* n = bc.pHeadAutoBreadcrumbNode; n; n = n->pNext)
		{
			LOGF("    breadcrumb: list=%S queue=%S executed=%u/%u",
			     n->pCommandListDebugNameW ? n->pCommandListDebugNameW : L"?",
			     n->pCommandQueueDebugNameW ? n->pCommandQueueDebugNameW : L"?",
			     n->pLastBreadcrumbValue ? *n->pLastBreadcrumbValue : 0, n->BreadcrumbCount);
		}
	}
	D3D12_DRED_PAGE_FAULT_OUTPUT pf = {};
	if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pf)))
		LOGF("    page fault VA = 0x%llX", (unsigned long long)pf.PageFaultVA);
	dred->Release();
}

static void DumpResourceDesc(const char* who, const D3D12_RESOURCE_DESC* d,
                               const D3D12_HEAP_PROPERTIES* hp, UINT heapFlags, UINT state)
{
	if (!d)
		return;
}

static void AllocationInfo(D3D12_RESOURCE_ALLOCATION_INFO* out, XboxDevice* device, UINT visibleMask,
                           UINT numResourceDescs, const D3D12_RESOURCE_DESC* pResourceDescs)
{
	if (!pResourceDescs || numResourceDescs == 0)
	{
		*out = device->real->GetResourceAllocationInfo(visibleMask, numResourceDescs, pResourceDescs);
		return;
	}
	D3D12_RESOURCE_DESC stackDescs[8];
	D3D12_RESOURCE_DESC* descs = stackDescs;
	if (numResourceDescs > ARRAYSIZE(stackDescs))
	{
		descs = (D3D12_RESOURCE_DESC*)HeapAlloc(GetProcessHeap(), 0,
		                                        sizeof(D3D12_RESOURCE_DESC) * numResourceDescs);
		if (!descs)
		{
			*out = device->real->GetResourceAllocationInfo(visibleMask, numResourceDescs, pResourceDescs);
			return;
		}
	}
	for (UINT i = 0; i < numResourceDescs; ++i)
		descs[i] = SanitizeResourceDesc(&pResourceDescs[i], "GetResourceAllocationInfo");
	*out = device->real->GetResourceAllocationInfo(visibleMask, numResourceDescs, descs);
	if (descs != stackDescs)
		HeapFree(GetProcessHeap(), 0, descs);
}

D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE XboxDevice::GetResourceAllocationInfo(
    IXboxDevice* device, UINT visibleMask, UINT numResourceDescs,
    const D3D12_RESOURCE_DESC* pResourceDescs)
{
	D3D12_RESOURCE_ALLOCATION_INFO* out = (D3D12_RESOURCE_ALLOCATION_INFO*)this;
	AllocationInfo(out, static_cast<XboxDevice*>(device), visibleMask, numResourceDescs, pResourceDescs);
	return out;
}

D3D12_HEAP_PROPERTIES* STDMETHODCALLTYPE XboxDevice::GetCustomHeapProperties(
    IXboxDevice* device, UINT nodeMask, D3D12_HEAP_TYPE heapType)
{
	D3D12_HEAP_PROPERTIES* out = (D3D12_HEAP_PROPERTIES*)this;
	*out = static_cast<XboxDevice*>(device)->real->GetCustomHeapProperties(nodeMask, heapType);
	return out;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateCommittedResource(const D3D12_HEAP_PROPERTIES* pHeapProperties, D3D12_HEAP_FLAGS HeapFlags,
    const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState,
    const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource, void** ppvResource)
{
	D3D12_RESOURCE_DESC d;
	if (pDesc)
		d = SanitizeResourceDesc(pDesc, "CreateCommittedResource");
	D3D12_HEAP_FLAGS hf = SanitizeHeapFlags(HeapFlags, "CreateCommittedResource");
	D3D12_RESOURCE_STATES st = SanitizeState(InitialResourceState, "CreateCommittedResource");
	HRESULT hr =
	    real->CreateCommittedResource(pHeapProperties, hf, pDesc ? &d : nullptr, st,
	                                        pOptimizedClearValue, riidResource, ppvResource);
	if (FAILED(hr))
	{
		LOGF("CreateCommittedResource FAILED hr=0x%08X", (unsigned)hr);
		if (hr == 0x887A0005 )
			GDKScarlett::D3D12X::ReportDeviceRemoved(real, "CreateCommittedResource");
		DumpResourceDesc("  sanitized", pDesc ? &d : nullptr, pHeapProperties, (unsigned)hf,
		                   (unsigned)st);
		DumpResourceDesc("  original ", pDesc, pHeapProperties, (unsigned)HeapFlags,
		                   (unsigned)InitialResourceState);
	}
	else if (ppvResource && *ppvResource)
	{
		if (pHeapProperties && pHeapProperties->Type == D3D12_HEAP_TYPE_UPLOAD)
			RecordUploadBuffer((ID3D12Resource*)*ppvResource);
		RecordGpuBuffer((ID3D12Resource*)*ppvResource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateHeap(const D3D12_HEAP_DESC* pDesc,
                                               REFIID riid, void** ppvHeap)
{
	return real->CreateHeap(pDesc, riid, ppvHeap);
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreatePlacedResource(ID3D12Heap* pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC* pDesc,
    D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid,
    void** ppvResource)
{
	D3D12_RESOURCE_DESC d;
	if (pDesc)
		d = SanitizeResourceDesc(pDesc, "CreatePlacedResource");
	HRESULT hr =
	    real->CreatePlacedResource(pHeap, HeapOffset, pDesc ? &d : nullptr,
	                                     SanitizeState(InitialState, "CreatePlacedResource"),
	                                     pOptimizedClearValue, riid, ppvResource);
	if (SUCCEEDED(hr) && ppvResource && *ppvResource && pHeap)
	{
		D3D12_HEAP_DESC hd = pHeap->GetDesc();
		if (hd.Properties.Type == D3D12_HEAP_TYPE_UPLOAD)
			RecordUploadBuffer((ID3D12Resource*)*ppvResource);
		RecordGpuBuffer((ID3D12Resource*)*ppvResource);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateReservedResource(const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState,
    const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource)
{
	D3D12_RESOURCE_DESC d;
	if (pDesc)
		d = SanitizeResourceDesc(pDesc, "CreateReservedResource");
	return real->CreateReservedResource(
	    pDesc ? &d : nullptr, SanitizeState(InitialState, "CreateReservedResource"),
	    pOptimizedClearValue, riid, ppvResource);
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateSharedHandle(ID3D12DeviceChild* pObject,
                                                       const SECURITY_ATTRIBUTES* pAttributes,
                                                       DWORD Access, LPCWSTR Name, HANDLE* pHandle)
{
	return real->CreateSharedHandle(pObject, pAttributes, Access, Name, pHandle);
}

HRESULT STDMETHODCALLTYPE XboxDevice::OpenSharedHandle(HANDLE NTHandle, REFIID riid,
                                                     void** ppvObj)
{
	return real->OpenSharedHandle(NTHandle, riid, ppvObj);
}

HRESULT STDMETHODCALLTYPE XboxDevice::OpenSharedHandleByName(LPCWSTR Name,
                                                           DWORD Access, HANDLE* pNTHandle)
{
	return real->OpenSharedHandleByName(Name, Access, pNTHandle);
}

HRESULT STDMETHODCALLTYPE XboxDevice::MakeResident(UINT NumObjects,
                                                 ID3D12Pageable* const* ppObjects)
{
	return real->MakeResident(NumObjects, ppObjects);
}

HRESULT STDMETHODCALLTYPE XboxDevice::Evict(UINT NumObjects,
                                          ID3D12Pageable* const* ppObjects)
{
	return real->Evict(NumObjects, ppObjects);
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateFence(UINT64 InitialValue,
                                                D3D12_FENCE_FLAGS Flags, REFIID riid,
                                                void** ppFence)
{
	return real->CreateFence(InitialValue, Flags, riid, ppFence);
}

HRESULT STDMETHODCALLTYPE XboxDevice::GetDeviceRemovedReason()
{
	return real->GetDeviceRemovedReason();
}

void STDMETHODCALLTYPE XboxDevice::GetCopyableFootprints(const D3D12_RESOURCE_DESC* pResourceDesc, UINT FirstSubresource,
    UINT NumSubresources, UINT64 BaseOffset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts,
    UINT* pNumRows, UINT64* pRowSizeInBytes, UINT64* pTotalBytes)
{
	real->GetCopyableFootprints(pResourceDesc, FirstSubresource, NumSubresources, BaseOffset,
	                                  pLayouts, pNumRows, pRowSizeInBytes, pTotalBytes);
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateQueryHeap(const D3D12_QUERY_HEAP_DESC* pDesc, REFIID riid,
                                                    void** ppvHeap)
{
	return real->CreateQueryHeap(pDesc, riid, ppvHeap);
}

HRESULT STDMETHODCALLTYPE XboxDevice::SetStablePowerState(BOOL Enable)
{
	return real->SetStablePowerState(Enable);
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC* pDesc,
    ID3D12RootSignature* pRootSignature, REFIID riid, void** ppvCommandSignature)
{
	if (!pDesc || !pDesc->pArgumentDescs || pDesc->NumArgumentDescs == 0)
		return real->CreateCommandSignature(pDesc, pRootSignature, riid, ppvCommandSignature);

	D3D12_INDIRECT_ARGUMENT_DESC stackArgs[16];
	D3D12_INDIRECT_ARGUMENT_DESC* args = stackArgs;
	if (pDesc->NumArgumentDescs > ARRAYSIZE(stackArgs))
	{
		args = (D3D12_INDIRECT_ARGUMENT_DESC*)HeapAlloc(
		    GetProcessHeap(), 0, sizeof(D3D12_INDIRECT_ARGUMENT_DESC) * pDesc->NumArgumentDescs);
		if (!args)
			return E_OUTOFMEMORY;
	}
	CopyMemory(args, pDesc->pArgumentDescs,
	           sizeof(D3D12_INDIRECT_ARGUMENT_DESC) * pDesc->NumArgumentDescs);

	for (UINT i = 0; i < pDesc->NumArgumentDescs; ++i)
	{
		const UINT t = (UINT)args[i].Type;
		switch (t)
		{
		case D3D12XBOX_IAT_DISPATCHX:
		case D3D12XBOX_IAT_DISPATCH_L2:
			args[i].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
			break;
#ifdef D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH
		case D3D12XBOX_IAT_DISPATCH_MESH_X:
			args[i].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
			break;
#endif
		default:
			if (t >= D3D12XBOX_IAT_PADDING)
				LOGF("CreateCommandSignature: arg %u UNMAPPED Xbox type %u", i, t);
			break;
		}
	}

	D3D12_COMMAND_SIGNATURE_DESC desc = *pDesc;
	desc.pArgumentDescs = args;

	HRESULT hr =
	    real->CreateCommandSignature(&desc, pRootSignature, riid, ppvCommandSignature);
	if (FAILED(hr))
		LOGF("CreateCommandSignature failed hr=0x%08X (stride=%u, %u args)", (unsigned)hr,
		     desc.ByteStride, desc.NumArgumentDescs);

	if (args != stackArgs)
		HeapFree(GetProcessHeap(), 0, args);
	return hr;
}

void STDMETHODCALLTYPE XboxDevice::GetResourceTiling(ID3D12Resource* pTiledResource, UINT* pNumTilesForEntireResource,
    D3D12_PACKED_MIP_INFO* pPackedMipDesc, D3D12_TILE_SHAPE* pStandardTileShapeForNonPackedMips,
    UINT* pNumSubresourceTilings, UINT FirstSubresourceTilingToGet,
    D3D12_SUBRESOURCE_TILING* pSubresourceTilingsForNonPackedMips)
{
	real->GetResourceTiling(pTiledResource, pNumTilesForEntireResource, pPackedMipDesc,
	                              pStandardTileShapeForNonPackedMips, pNumSubresourceTilings,
	                              FirstSubresourceTilingToGet, pSubresourceTilingsForNonPackedMips);
}

LUID STDMETHODCALLTYPE XboxDevice::GetAdapterLuid()
{
	return real->GetAdapterLuid();
}

HRESULT STDMETHODCALLTYPE XboxDevice::SetDriverHintX(void*, void*, void*, void*, void*,
                                                   void*)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE XboxDevice::SetHangCallbacksX(void*, void*, void*, void*,
                                                      void*, void*)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE XboxDevice::ReportGpuHangX(void*, void*, void*, void*, void*,
                                                   void*)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE XboxDevice::SetDebugFlagsX(void*, void*, void*, void*, void*,
                                                   void*)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE XboxDevice::SetDebugCallbackX(void*, void*, void*, void*,
                                                      void*, void*)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE XboxDevice::EnableManualGraphicsTLBInvalidationX(void*, void*,
                                                                         void*, void*, void*, void*)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE XboxDevice::RegisterCustomFenceLocationX(void*, void*, void*,
                                                                 void*, void*, void*)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE XboxDevice::UnregisterCustomFenceLocationX(void*, void*, void*,
                                                                   void*, void*, void*)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE XboxDevice::SetDebugErrorFilterX(void*, void*, void*, void*,
                                                         void*, void*)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE XboxDevice::SetGpuMemoryPriorityX(void*, void*, void*, void*,
                                                          void*, void*)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE XboxDevice::RegisterPagePoolX(void*, void*, void*, void*,
                                                      void*, void*)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE XboxDevice::UnregisterPagePoolX(void*, void*, void*, void*,
                                                        void*, void*)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE XboxDevice::SetFrameIntervalX(void*, void*, void*, void*,
                                                      void*, void*)
{
	return S_OK;
}

UINT STDMETHODCALLTYPE XboxDevice::GetDebugFlagsX(void*, void*, void*, void*, void*,
                                                void*)
{
	return 0;
}

void* GDKScarlett::D3D12X::ReservedResourceForVa(UINT64 va, UINT* startTileOut, UINT* tileCountOut)
{
	void* r = nullptr;
	AcquireSRWLockShared(&GReservedLock);
	for (auto& e : GReserved)
	{
		if (va >= e.va && va < e.va + e.bytes)
		{
			r = e.res;
			if (startTileOut)
				*startTileOut = (UINT)((va - e.va) / kXcPage);
			if (tileCountOut)
				*tileCountOut = e.tiles;
			break;
		}
	}
	ReleaseSRWLockShared(&GReservedLock);
	return r;
}

void* GDKScarlett::D3D12X::PagePoolHeap(void* devv, UINT64 poolVa, UINT pageCount)
{
	ID3D12Device* dev = (ID3D12Device*)devv;
	if (!dev || !poolVa || !pageCount)
		return nullptr;
	ID3D12Heap* h = nullptr;
	AcquireSRWLockExclusive(&GPoolLock);
	auto it = GPoolHeaps.find(poolVa);
	if (it != GPoolHeaps.end())
	{
		h = it->second;
	}
	else
	{
		D3D12_HEAP_DESC hd{};
		hd.SizeInBytes = (UINT64)pageCount * kXcPage;
		hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		hd.Properties.CreationNodeMask = 1;
		hd.Properties.VisibleNodeMask = 1;
		hd.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		hd.Flags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
		if (SUCCEEDED(dev->CreateHeap(&hd, __uuidof(ID3D12Heap), (void**)&h)) && h)
		{
			GPoolHeaps[poolVa] = h;
		}
	}
	ReleaseSRWLockExclusive(&GPoolLock);
	return h;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreatePlacedResourceX(D3D12_GPU_VIRTUAL_ADDRESS ResourceLocation, const D3D12_RESOURCE_DESC* pDesc,
    D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid,
    void** ppvResource)
{
	if (!ppvResource)
		return E_POINTER;
	*ppvResource = nullptr;
	if (!pDesc)
		return E_INVALIDARG;

	D3D12_RESOURCE_DESC d = SanitizeResourceDesc(pDesc, "CreatePlacedResourceX");
	D3D12_RESOURCE_STATES st = SanitizeState(InitialState, "CreatePlacedResourceX");

	D3D12_HEAP_PROPERTIES hp = {};
	hp.Type = D3D12_HEAP_TYPE_DEFAULT;
	hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	hp.CreationNodeMask = 1;
	hp.VisibleNodeMask = 1;

	HRESULT hr = real->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, st,
	                                                 pOptimizedClearValue, riid, ppvResource);
	if (SUCCEEDED(hr))
	{
		RecordPlacedX(*ppvResource, (UINT64)ResourceLocation, &d, (UINT)st);
	}
	if (FAILED(hr))
	{
		DumpResourceDesc("  sanitized", &d, &hp, 0, (unsigned)st);
		DumpResourceDesc("  original ", pDesc, &hp, 0, (unsigned)InitialState);
		if (hr == 0x887A0005 )
			GDKScarlett::D3D12X::ReportDeviceRemoved(real, "CreatePlacedResourceX");
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateComponentPlacedResourceX(void*, void*, void*,
                                                                   void*, void*, void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreatePlacedShaderResourceViewX(void*, void*,
                                                                    void*, void*, void*, void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreatePlacedUnorderedAccessViewX(void*, void*,
                                                                     void*, void*, void*, void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateCommandQueueX(const D3D12XBOX_COMMAND_QUEUE_DESC* pDesc,
                                                        REFIID riid, void** ppCommandQueue)
{
	if (!pDesc)
		return E_INVALIDARG;

	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Type = (pDesc->Type == (D3D12_COMMAND_LIST_TYPE)D3D12XBOX_COMMAND_LIST_TYPE_DMA)
	                ? D3D12_COMMAND_LIST_TYPE_COPY
	                : pDesc->Type;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags = pDesc->Flags;
	desc.NodeMask = 0;

	HRESULT hr = real->CreateCommandQueue(&desc, riid, ppCommandQueue);

	if (SUCCEEDED(hr) && ppCommandQueue && *ppCommandQueue)
	{
		ID3D12CommandQueue* realQueue = (ID3D12CommandQueue*)*ppCommandQueue;
		*ppCommandQueue = XboxCommandQueueWrap(realQueue, (ID3D12Device*)this);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE XboxDevice::SerializeGraphicsPipelineStateX(void*, void*,
                                                                    void*, void*, void*, void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::DeserializeGraphicsPipelineStateX(void*, void*,
                                                                      void*, void*, void*, void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::GetDefaultMSAAParametersX(void*, void*, void*,
                                                              void*, void*, void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateGraphicsPipelineStateX(void*, void*, void*,
                                                                 void*, void*, void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateSamplerX(void*, void*, void*, void*, void*,
                                                   void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateComputePipelineStateX(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pBaseDesc, UINT NumExtendedDescs,
    const void* ,
    REFIID riid, void** ppPipelineState)
{
	return CreateComputePSO(this, pBaseDesc, riid, ppPipelineState);
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateDerivedComputePipelineStateX(void*, void*,
                                                                       void*, void*, void*,
                                                                       void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::SerializeComputePipelineStateX(void*, void*, void*,
                                                                   void*, void*, void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::DeserializeComputePipelineStateX(void*, void*,
                                                                     void*, void*, void*, void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::GetGpuHardwareConfigurationX(void*, void*, void*,
                                                                 void*, void*, void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreatePlacedRawShaderResourceViewX(void*, void*,
                                                                       void*, void*, void*,
                                                                       void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreatePlacedRawUnorderedAccessViewX(void*, void*,
                                                                        void*, void*, void*,
                                                                        void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateCommandListX(const D3D12XBOX_COMMAND_LIST_DESC* pDesc,
                                                       ID3D12CommandAllocator* pAllocator,
                                                       ID3D12PipelineState* pInitialState,
                                                       REFIID riid, void** ppCommandList)
{
	if (!pDesc)
		return E_INVALIDARG;
	(void)riid;

	D3D12_COMMAND_LIST_TYPE type =
	    (pDesc->Type == (D3D12_COMMAND_LIST_TYPE)D3D12XBOX_COMMAND_LIST_TYPE_DMA)
	        ? D3D12_COMMAND_LIST_TYPE_COPY
	        : pDesc->Type;

	ID3D12GraphicsCommandList* realList = nullptr;
	HRESULT hr = real->CreateCommandList(0, type, pAllocator, pInitialState,
	                                           IID_ID3D12GraphicsCommandList, (void**)&realList);

	if (SUCCEEDED(hr) && realList)
	{
		ID3D12GraphicsCommandList* wrapped = XboxCommandListWrap(realList, (ID3D12Device*)this);
		if (ppCommandList)
			*ppCommandList = wrapped;
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateCommittedResourceX(void*, void*, void*,
                                                             void*, void*, void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateComponentPlacedResourceX1(void*, void*,
                                                                    void*, void*, void*, void* a6)
{
	return E_NOTIMPL;
}

static DWORD WINAPI FramePacer(void*)
{
	for (;;)
	{
		Sleep(16);
		AcquireSRWLockShared(&GFrameEvLock);
		for (UINT i = 0; i < GFrameEvCount; ++i)
			SetEvent(GFrameEv[i]);
		ReleaseSRWLockShared(&GFrameEvLock);
	}
}

HRESULT STDMETHODCALLTYPE XboxDevice::ScheduleFrameEventX(UINT Type,
                                                        UINT IntervalOffsetUs,
                                                        FrameObjList* pList, UINT Flags, void*,
                                                        void*)
{
	UINT registered = 0;
	if (pList && pList->pObjects && pList->Count)
	{
		AcquireSRWLockExclusive(&GFrameEvLock);
		for (UINT i = 0; i < pList->Count && GFrameEvCount < ARRAYSIZE(GFrameEv); ++i)
		{
			HANDLE h = pList->pObjects[i];
			if (!h)
				continue;
			bool dup = false;
			for (UINT j = 0; j < GFrameEvCount; ++j)
				if (GFrameEv[j] == h)
				{
					dup = true;
					break;
				}
			if (!dup)
			{
				GFrameEv[GFrameEvCount++] = h;
				++registered;
			}
		}
		if (!GFramePacer)
			GFramePacer = CreateThread(nullptr, 0, FramePacer, nullptr, 0, nullptr);
		ReleaseSRWLockExclusive(&GFrameEvLock);
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE XboxDevice::WaitFrameEventX(UINT Type, UINT TimeOutInMs,
                                                    void* , UINT Flags,
                                                    UINT64* pToken)
{
	static volatile LONG64 nextToken = 0;
	if (pToken)
		*pToken = (UINT64)InterlockedIncrement64(&nextToken);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE XboxDevice::GetFrameStatisticsX(UINT64 Token, UINT TypeSet,
                                                        UINT* pCount, BYTE* pStatistics, void*,
                                                        void*)
{
	static LONG callCount = 0;
	LONG c = InterlockedIncrement(&callCount);
	if (!pCount || !pStatistics)
	{
		if (pCount)
			*pCount = 0;
		return S_FALSE;
	}
	const UINT cap = *pCount;
	UINT written = 0;
	LARGE_INTEGER qpc;
	QueryPerformanceCounter(&qpc);
	const UINT64 now = (UINT64)qpc.QuadPart;
	struct FrameStat
	{
		UINT Type;
		UINT pad;
		BYTE u[0x30];
	};
	FrameStat* out = (FrameStat*)pStatistics;
	auto emit = [&](UINT type) -> FrameStat*
	{
		if (written >= cap)
			return nullptr;
		FrameStat* e = &out[written++];
		memset(e, 0, sizeof(*e));
		e->Type = type;
		return e;
	};
	if (TypeSet & 0x1)
	{
		if (FrameStat* e = emit(0x1))
		{
			*(UINT*)(e->u + 0x00) = 16667;
			*(UINT*)(e->u + 0x04) = 1;
		}
	}
	if (TypeSet & 0x2)
	{
		if (FrameStat* e = emit(0x2))
		{
			*(UINT64*)(e->u + 0x08) = now;
			*(UINT64*)(e->u + 0x10) = now;
			*(UINT64*)(e->u + 0x18) = now;
		}
	}
	if (TypeSet & 0x100)
	{
		if (FrameStat* e = emit(0x100))
		{
			*(UINT64*)(e->u + 0x08) = now;
			*(UINT64*)(e->u + 0x10) = 1000;
		}
	}
	if (TypeSet & 0x1000)
	{
		if (FrameStat* e = emit(0x1000))
		{
			e->u[1] = 1;
			e->u[2] = 1;
			*(UINT64*)(e->u + 0x08) = now;
			*(UINT64*)(e->u + 0x10) = now;
			*(UINT64*)(e->u + 0x18) = now;
			*(UINT64*)(e->u + 0x20) = 1000;
			e->u[0x28] = 1;
			*(USHORT*)(e->u + 0x2a) = 1;
		}
	}
	if (TypeSet & 0x10000)
	{
		if (FrameStat* e = emit(0x10000))
		{
			*(UINT64*)(e->u + 0x08) = now;
			*(UINT64*)(e->u + 0x10) = now;
			*(float*)(e->u + 0x18) = 100.0f;
		}
	}
	*pCount = written;
	return written ? S_OK : S_FALSE;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateCounterSetX(void*, void*, void*, void*,
                                                      void*, void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateCommittedOpaqueResourceX(void*, void*, void*,
                                                                   void*, void*, void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreatePlacedOpaqueResourceX(void*, void*, void*,
                                                                void*, void*, void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::GetOpaqueResourceAllocationInfoX(void*, void*,
                                                                     void*, void*, void*, void* a6)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateFeedbackUnorderedAccessViewX(void*, void*,
                                                                       void*, void*, void*,
                                                                       void* a6)
{
	return E_NOTIMPL;
}

template <typename T> static T* RealAs(XboxDevice* device, REFIID iid)
{
	T* p = nullptr;
	if (FAILED(device->real->QueryInterface(iid, (void**)&p)))
		return nullptr;
	return p;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreatePipelineLibrary(const void* pLibraryBlob,
                                                          SIZE_T BlobLength, REFIID riid,
                                                          void** ppPipelineLibrary)
{
	ID3D12Device1* d1 = RealAs<ID3D12Device1>(this, __uuidof(ID3D12Device1));
	if (!d1)
		return E_NOINTERFACE;
	HRESULT hr = d1->CreatePipelineLibrary(pLibraryBlob, BlobLength, riid, ppPipelineLibrary);
	d1->Release();
	return hr;
}

HRESULT STDMETHODCALLTYPE XboxDevice::SetEventOnMultipleFenceCompletion(ID3D12Fence* const* ppFences, const UINT64* pFenceValues, UINT NumFences,
    D3D12_MULTIPLE_FENCE_WAIT_FLAGS Flags, HANDLE hEvent)
{
	ID3D12Device1* d1 = RealAs<ID3D12Device1>(this, __uuidof(ID3D12Device1));
	if (!d1)
		return E_NOINTERFACE;
	HRESULT hr =
	    d1->SetEventOnMultipleFenceCompletion(ppFences, pFenceValues, NumFences, Flags, hEvent);
	d1->Release();
	return hr;
}

HRESULT STDMETHODCALLTYPE XboxDevice::SetResidencyPriority(UINT NumObjects, ID3D12Pageable* const* ppObjects,
                        const D3D12_RESIDENCY_PRIORITY* pPriorities)
{
	ID3D12Device1* d1 = RealAs<ID3D12Device1>(this, __uuidof(ID3D12Device1));
	if (!d1)
		return E_NOINTERFACE;
	HRESULT hr = d1->SetResidencyPriority(NumObjects, ppObjects, pPriorities);
	d1->Release();
	return hr;
}

static SIZE_T PsoSubobjectSize(UINT type)
{
#define SO(T)                                                                                      \
	(SIZE_T)(                                                                                      \
	    ((((4u + (UINT) __alignof(T) - 1u) & ~((UINT) __alignof(T) - 1u)) + sizeof(T)) + 7u) &     \
	    ~7u)
	switch (type)
	{
	case 0:
		return SO(ID3D12RootSignature*);
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
		return SO(D3D12_SHADER_BYTECODE);
	case 7:
		return SO(D3D12_STREAM_OUTPUT_DESC);
	case 8:
		return SO(D3D12_BLEND_DESC);
	case 9:
		return SO(UINT);
	case 10:
		return SO(D3D12_RASTERIZER_DESC);
	case 11:
		return SO(D3D12_DEPTH_STENCIL_DESC);
	case 12:
		return SO(D3D12_INPUT_LAYOUT_DESC);
	case 13:
		return SO(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE);
	case 14:
		return SO(D3D12_PRIMITIVE_TOPOLOGY_TYPE);
	case 15:
		return SO(D3D12_RT_FORMAT_ARRAY);
	case 16:
		return SO(DXGI_FORMAT);
	case 17:
		return SO(DXGI_SAMPLE_DESC);
	case 18:
		return SO(UINT);
	case 19:
		return SO(D3D12_CACHED_PIPELINE_STATE);
	case 20:
		return SO(D3D12_PIPELINE_STATE_FLAGS);
	case 21:
		return SO(D3D12_DEPTH_STENCIL_DESC1);
	case 22:
		return SO(D3D12_VIEW_INSTANCING_DESC);
	case 24:
		return 0x20;
	default:
		return 0;
	}
#undef SO
}

static UINT ScanRenderTargetCount(const void* in, SIZE_T inSize)
{
	const BYTE* p = (const BYTE*)in;
	SIZE_T off = 0;
	while (off + sizeof(UINT) <= inSize)
	{
		UINT type = *(const UINT*)(p + off);
		SIZE_T sz = PsoSubobjectSize(type);
		if (sz == 0 || off + sz > inSize)
			break;
		if (type == 15)
		{
			const D3D12_RT_FORMAT_ARRAY* rt = (const D3D12_RT_FORMAT_ARRAY*)(p + off + 4);
			return rt->NumRenderTargets;
		}
		off += sz;
	}
	return 0;
}

static void RegisterPsoKeys(void* pso, const char* keys)
{
	AcquireSRWLockExclusive(&GPsoKeyLock);
	GPsoKeys[pso] = keys ? keys : " (unknown)";
	ReleaseSRWLockExclusive(&GPsoKeyLock);
}

static bool PipelineFullyCacheable(const void* in, SIZE_T inSize)
{
	const BYTE* p = (const BYTE*)in;
	SIZE_T off = 0;
	bool sawSubstitutable = false;
	while (off + sizeof(UINT) <= inSize)
	{
		UINT type = *(const UINT*)(p + off);
		SIZE_T sz = PsoSubobjectSize(type);
		if (sz == 0 || off + sz > inSize)
			break;
		if (type >= 1 && type <= 6)
		{
			const D3D12_SHADER_BYTECODE* sb = (const D3D12_SHADER_BYTECODE*)(p + off + 8);
			if (sb->pShaderBytecode && sb->BytecodeLength)
			{
				if (type == 3 || type == 4 || type == 5)
					return false;
				if (type == 1 || type == 2)
				{
					if (!GDKScarlett::D3D12X::HasCacheEntry(sb))
						return false;
					sawSubstitutable = true;
				}
			}
		}
		off += sz;
	}
	return sawSubstitutable;
}

static SIZE_T RewritePsoStream(const void* in, SIZE_T inSize, BYTE* out, SIZE_T outCap,
                                 bool allowCache = true, bool* rawXboxStage = nullptr,
                                 const D3D12_SHADER_BYTECODE* vsForce = nullptr)
{
	LONG64 rewriteStart = NowTicks();
	struct RewriteTimer
	{
		LONG64 start;
		~RewriteTimer()
		{
			NoteTicks(&GPsoRewriteTicks, &GPsoRewriteCalls, &GPsoRewriteMaxTicks,
			          NowTicks() - start);
		}
	} rewriteTimer{ rewriteStart };
	const BYTE* p = (const BYTE*)in;
	const bool hasRenderTarget = ScanRenderTargetCount(in, inSize) > 0;
	SIZE_T off = 0, w = 0;
	bool changed = false;
	while (off + sizeof(UINT) <= inSize)
	{
		UINT type = *(const UINT*)(p + off);
		SIZE_T sz = PsoSubobjectSize(type);
		if (sz == 0 || off + sz > inSize)
		{
			LOGF("PSO stream: stopping at offset %llu (type %u, unsized) - %llu of %llu bytes kept",
			     (unsigned long long)off, type, (unsigned long long)w, (unsigned long long)inSize);
			changed = true;
			break;
		}

		if (type >= XBOX_PSO_SUBOBJ_FIRST || (!allowCache && type == 7))
		{
			changed = true;
		}
		else
		{
			if (w + sz > outCap)
				return 0;
			memcpy(out + w, p + off, sz);

			if (type == 8 && allowCache)
			{
				BYTE* bd = out + w + 4;
				BYTE* rt = bd + 8;
				if (*(const UINT*)(rt + 0) == 0)
				{
					UINT srcBlend = 2u;
					*(UINT*)(rt + 0) = 1;
					*(UINT*)(rt + 8) = srcBlend;
					*(UINT*)(rt + 12) = 6;
					*(UINT*)(rt + 16) = 1;
					*(UINT*)(rt + 20) = 2;
					*(UINT*)(rt + 24) = 6;
					*(UINT*)(rt + 28) = 1;
					InterlockedIncrement(&GPsoBlendForced);
					changed = true;
				}
			}

			if (type >= 1 && type <= 6)
			{
				D3D12_SHADER_BYTECODE* dst = (D3D12_SHADER_BYTECODE*)(out + w + 8);
				if (!allowCache && (type == 3 || type == 4 || type == 5))
				{
					if (dst->pShaderBytecode || dst->BytecodeLength)
					{
						dst->pShaderBytecode = nullptr;
						dst->BytecodeLength = 0;
						changed = true;
					}
				}
				else if (dst->pShaderBytecode || dst->BytecodeLength)
				{
					if (type == 1 && vsForce && vsForce->pShaderBytecode)
					{
						*dst = *vsForce;
						changed = true;
					}
					else
					{
						D3D12_SHADER_BYTECODE recompiled{};
						if (GDKScarlett::D3D12X::TryRecompileToDxil(type, hasRenderTarget, dst,
						                                            &recompiled, allowCache))
						{
							*dst = recompiled;
							changed = true;
						}
						else if (rawXboxStage)
						{
							*rawXboxStage = true;
						}
					}
				}
			}
			w += sz;
		}
		off += sz;
	}
	return changed ? w : 0;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreatePipelineState(const D3D12_PIPELINE_STATE_STREAM_DESC* pDesc, REFIID riid,
                       void** ppPipelineState)
{
	ID3D12Device2* d2 = RealAs<ID3D12Device2>(this, __uuidof(ID3D12Device2));
	if (!d2)
	{
		LOGF("CreatePipelineState: real device exposes no ID3D12Device2");
		return E_NOINTERFACE;
	}
	D3D12_PIPELINE_STATE_STREAM_DESC desc = pDesc ? *pDesc : D3D12_PIPELINE_STATE_STREAM_DESC{};
	BYTE* rebuilt = nullptr;
	bool phStream = false;
	if (pDesc && pDesc->pPipelineStateSubobjectStream && pDesc->SizeInBytes)
	{
		rebuilt = (BYTE*)HeapAlloc(GetProcessHeap(), 0, pDesc->SizeInBytes);
		if (rebuilt)
		{
			bool rawStage = false;
			GDKScarlett::D3D12X::BeginPsoKeyCapture();
			const bool useCache =
			    PipelineFullyCacheable(pDesc->pPipelineStateSubobjectStream, pDesc->SizeInBytes);
			InterlockedIncrement(&GPsoTotal);
			InterlockedIncrement(useCache ? &GPsoCached : &GPsoPlaceholderCount);
			SIZE_T n = RewritePsoStream(pDesc->pPipelineStateSubobjectStream, pDesc->SizeInBytes,
			                              rebuilt, pDesc->SizeInBytes, useCache, &rawStage);
			phStream = !useCache;
			if (rawStage)
			{
				InterlockedIncrement(&GPsoPlaceholderCount);
				n = RewritePsoStream(pDesc->pPipelineStateSubobjectStream, pDesc->SizeInBytes,
				                       rebuilt, pDesc->SizeInBytes,
				                       false);
				phStream = true;
			}
			if (n)
			{
				desc.pPipelineStateSubobjectStream = rebuilt;
				desc.SizeInBytes = n;
			}
		}
	}

	const UINT64 iqMark = GDKScarlett::D3D12X::MarkInfoQueue(real);
	HRESULT hr = TimedCreatePipelineState(d2, &desc, riid, ppPipelineState);

	auto minimizeStream = [](BYTE* s, SIZE_T n) -> SIZE_T
	{
		SIZE_T off = 0;
		while (off + sizeof(UINT) <= n)
		{
			UINT type = *(const UINT*)(s + off);
			SIZE_T sz = PsoSubobjectSize(type);
			if (sz == 0 || off + sz > n)
				break;
			if (type == 12)
			{
				memmove(s + off, s + off + sz, n - off - sz);
				n -= sz;
				continue;
			}
			if (type == 14)
			{
				UINT* t = (UINT*)(s + off + 4);
				if (*t == 0 || *t > 4)
					*t = 3;
			}
			off += sz;
		}
		return n;
	};

	if (FAILED(hr) && rebuilt && pDesc && pDesc->pPipelineStateSubobjectStream)
	{
		GDKScarlett::D3D12X::DrainInfoQueue(real, "PSO-CACHE-REJECT", iqMark);
		if (!phStream)
		{
			const D3D12_SHADER_BYTECODE* vsBc = nullptr;
			const D3D12_SHADER_BYTECODE* psBc = nullptr;
			const D3D12_INPUT_LAYOUT_DESC* ilD = nullptr;
			const BYTE* pp = (const BYTE*)pDesc->pPipelineStateSubobjectStream;
			SIZE_T o = 0;
			while (o + sizeof(UINT) <= pDesc->SizeInBytes)
			{
				UINT t = *(const UINT*)(pp + o);
				SIZE_T s = PsoSubobjectSize(t);
				if (s == 0 || o + s > pDesc->SizeInBytes)
					break;
				if (t == 1)
					vsBc = (const D3D12_SHADER_BYTECODE*)(pp + o + 8);
				if (t == 2)
					psBc = (const D3D12_SHADER_BYTECODE*)(pp + o + 8);
				if (t == 12)
					ilD = (const D3D12_INPUT_LAYOUT_DESC*)(pp + o + 8);
				o += s;
			}
			D3D12_SHADER_BYTECODE fixedVs{};
			if (vsBc && vsBc->pShaderBytecode && psBc && psBc->pShaderBytecode &&
			    GDKScarlett::D3D12X::TryLinkFixVs(vsBc, psBc, ilD, &fixedVs))
			{
				BYTE* lf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, pDesc->SizeInBytes);
				if (lf)
				{
					SIZE_T nl = RewritePsoStream(pDesc->pPipelineStateSubobjectStream,
					                               pDesc->SizeInBytes, lf, pDesc->SizeInBytes,
					                               true, nullptr, &fixedVs);
					if (nl)
					{
						D3D12_PIPELINE_STATE_STREAM_DESC dl = *pDesc;
						dl.pPipelineStateSubobjectStream = lf;
						dl.SizeInBytes = nl;
						const UINT64 lfMark = GDKScarlett::D3D12X::MarkInfoQueue(real);
						HRESULT hrl = TimedCreatePipelineState(d2, &dl, riid, ppPipelineState);
						if (SUCCEEDED(hrl))
						{
							if (ppPipelineState && *ppPipelineState)
							{
								AcquireSRWLockExclusive(&GPsoStreamLock);
								GPsoStreams[*ppPipelineState].assign(lf, lf + nl);
								ReleaseSRWLockExclusive(&GPsoStreamLock);
							}
							hr = hrl;
						}
						else
						{
							GDKScarlett::D3D12X::DrainInfoQueue(real, "PSO-LINKFIX-REJECT",
							                                    lfMark);
						}
					}
					HeapFree(GetProcessHeap(), 0, lf);
				}
			}
		}
		if (FAILED(hr))
		{
			BYTE* fallback = (BYTE*)HeapAlloc(GetProcessHeap(), 0, pDesc->SizeInBytes);
			if (fallback)
			{
				SIZE_T n2 = RewritePsoStream(pDesc->pPipelineStateSubobjectStream,
				                               pDesc->SizeInBytes, fallback, pDesc->SizeInBytes,
				                               false);
				if (n2)
				{
					D3D12_PIPELINE_STATE_STREAM_DESC d3 = *pDesc;
					d3.pPipelineStateSubobjectStream = fallback;
					d3.SizeInBytes = n2;
					HRESULT hr2 = TimedCreatePipelineState(d2, &d3, riid, ppPipelineState);
					phStream = true;
					if (SUCCEEDED(hr2))
					{
						if (ppPipelineState && *ppPipelineState)
						{
							AcquireSRWLockExclusive(&GPsoStreamLock);
							GPsoStreams[*ppPipelineState].assign(fallback, fallback + n2);
							ReleaseSRWLockExclusive(&GPsoStreamLock);
						}
						hr = hr2;
					}
					else
					{
						SIZE_T n3 = minimizeStream(fallback, n2);
						D3D12_PIPELINE_STATE_STREAM_DESC d4 = *pDesc;
						d4.pPipelineStateSubobjectStream = fallback;
						d4.SizeInBytes = n3;
						HRESULT hr3 = TimedCreatePipelineState(d2, &d4, riid, ppPipelineState);
						if (SUCCEEDED(hr3))
						{
							if (ppPipelineState && *ppPipelineState)
							{
								AcquireSRWLockExclusive(&GPsoStreamLock);
								GPsoStreams[*ppPipelineState].assign(fallback, fallback + n3);
								ReleaseSRWLockExclusive(&GPsoStreamLock);
							}
							hr = hr3;
						}
					}
				}
				HeapFree(GetProcessHeap(), 0, fallback);
			}
		}
	}

	if (SUCCEEDED(hr))
	{
		if (ppPipelineState && *ppPipelineState)
		{
			const char* keys = GDKScarlett::D3D12X::PsoKeyCapture();
			AcquireSRWLockExclusive(&GPsoKeyLock);
			GPsoKeys[*ppPipelineState] = (keys && *keys) ? keys : " (placeholder)";
			if (phStream)
				GPsoPlaceholder.insert(*ppPipelineState);
			else
				GPsoPlaceholder.erase(*ppPipelineState);
			ReleaseSRWLockExclusive(&GPsoKeyLock);
			AcquireSRWLockExclusive(&GPsoStreamLock);
			if (!GPsoStreams.count(*ppPipelineState) && desc.pPipelineStateSubobjectStream &&
			    desc.SizeInBytes)
				GPsoStreams[*ppPipelineState].assign(
				    (const BYTE*)desc.pPipelineStateSubobjectStream,
				    (const BYTE*)desc.pPipelineStateSubobjectStream + desc.SizeInBytes);
			ReleaseSRWLockExclusive(&GPsoStreamLock);
		}
	}
	else
	{
		LOGF("CreatePipelineState FAILED hr=0x%08X (stream %llu -> %llu bytes)", (unsigned)hr,
		     pDesc ? (unsigned long long)pDesc->SizeInBytes : 0ull,
		     (unsigned long long)desc.SizeInBytes);
		char stages[128];
		stages[0] = 0;
		if (pDesc && pDesc->pPipelineStateSubobjectStream)
		{
			const BYTE* pp = (const BYTE*)pDesc->pPipelineStateSubobjectStream;
			SIZE_T o = 0;
			while (o + sizeof(UINT) <= pDesc->SizeInBytes)
			{
				UINT t = *(const UINT*)(pp + o);
				SIZE_T s = PsoSubobjectSize(t);
				if (s == 0 || o + s > pDesc->SizeInBytes)
					break;
				if (t >= 1 && t <= 6)
				{
					const D3D12_SHADER_BYTECODE* sb = (const D3D12_SHADER_BYTECODE*)(pp + o + 8);
				}
				o += s;
			}
		}
		GDKScarlett::D3D12X::DrainInfoQueue(real, "CreatePipelineState", iqMark);
	}

	if (rebuilt)
		HeapFree(GetProcessHeap(), 0, rebuilt);
	d2->Release();
	return hr;
}

static bool PatchStreamSubobject(BYTE* stream, SIZE_T size, UINT soType, const void* bytes,
                                   SIZE_T len)
{
	SIZE_T off = 0;
	while (off + sizeof(UINT) <= size)
	{
		UINT t = *(const UINT*)(stream + off);
		SIZE_T s = PsoSubobjectSize(t);
		if (s == 0 || off + s > size)
			return false;
		if (t == soType)
		{
			if (off + 4 + len > size)
				return false;
			memcpy(stream + off + 4, bytes, len);
			return true;
		}
		off += s;
	}
	return false;
}

HRESULT STDMETHODCALLTYPE XboxDevice::CreateDerivedGraphicsPipelineStateX(ID3D12PipelineState* pSrcPipelineState, UINT NumDescs, const void* pDescs,
    REFIID riid, void** ppDerivedPipelineState)
{
	if (!ppDerivedPipelineState)
		return E_POINTER;
	*ppDerivedPipelineState = nullptr;
	if (!pSrcPipelineState)
		return E_INVALIDARG;

	static LONG callCount = 0;
	LONG call = InterlockedIncrement(&callCount);

	std::vector<BYTE> stream;
	{
		AcquireSRWLockShared(&GPsoStreamLock);
		auto it = GPsoStreams.find(pSrcPipelineState);
		if (it != GPsoStreams.end())
			stream = it->second;
		ReleaseSRWLockShared(&GPsoStreamLock);
	}

	const BYTE* dp = (const BYTE*)pDescs;
	bool descsOk = dp && NumDescs > 0;
	for (UINT i = 0; descsOk && i < NumDescs; ++i)
		if (*(const UINT*)(dp + i * kDerivedDescStride) > 7)
			descsOk = false;

	if (descsOk && !stream.empty())
	{
		UINT applied = 0, relevant = 0;
		for (UINT i = 0; i < NumDescs; ++i)
		{
			const BYTE* e = dp + i * kDerivedDescStride;
			UINT t = *(const UINT*)e;
			const void* payload = e + 4;
			bool patched = false;
			switch (t)
			{
			case 0:
			{
				++relevant;
				BYTE ds[52];
				memcpy(ds, payload, 52);
				if (((UINT*)ds)[3] != 0)
				{
					((UINT*)ds)[3] = 0;
				}
				patched = PatchStreamSubobject(stream.data(), stream.size(), 11, ds, 52) ||
				          PatchStreamSubobject(stream.data(), stream.size(), 21, ds, 52);
				if (!patched)
				{
					SIZE_T sz = PsoSubobjectSize(11);
					size_t base = stream.size();
					stream.resize(base + sz, 0);
					*(UINT*)(stream.data() + base) = 11;
					memcpy(stream.data() + base + 4, ds, 52);
					patched = true;
				}
				break;
			}
			case 1:
				++relevant;
				patched = PatchStreamSubobject(stream.data(), stream.size(), 10, payload, 44);
				break;
			case 2:
			{
				++relevant;
				static UINT seenM[8] = {};
				static LONG seenMN = 0;
				UINT wm = *(const BYTE*)((const BYTE*)payload + 44);
				bool knownM = false;
				LONG nm = seenMN;
				for (LONG j = 0; j < nm && j < 8; ++j)
					if (seenM[j] == wm)
					{
						knownM = true;
						break;
					}
				if (!knownM)
				{
					LONG idx = InterlockedIncrement(&seenMN) - 1;
					if (idx < 8)
					{
						seenM[idx] = wm;
					}
				}
				patched = PatchStreamSubobject(stream.data(), stream.size(), 8, payload, 328);
				break;
			}
			case 3:
				++relevant;
				patched = PatchStreamSubobject(stream.data(), stream.size(), 9, payload, 4);
				break;
			default:
				break;
			}
			if (patched)
				++applied;
		}
		ID3D12Device2* d2 = RealAs<ID3D12Device2>(this, __uuidof(ID3D12Device2));
		if (d2)
		{
			D3D12_PIPELINE_STATE_STREAM_DESC d = {stream.size(), stream.data()};
			HRESULT hr = TimedCreatePipelineState(d2, &d, riid, ppDerivedPipelineState);
			d2->Release();
			if (SUCCEEDED(hr) && *ppDerivedPipelineState)
			{
				{
					AcquireSRWLockExclusive(&GPsoKeyLock);
					auto it = GPsoKeys.find(pSrcPipelineState);
					if (it != GPsoKeys.end())
						GPsoKeys[*ppDerivedPipelineState] = it->second;
					ReleaseSRWLockExclusive(&GPsoKeyLock);
				}
				AcquireSRWLockExclusive(&GPsoStreamLock);
				GPsoStreams[*ppDerivedPipelineState] = stream;
				ReleaseSRWLockExclusive(&GPsoStreamLock);
				static LONG successCount = 0;
				LONG c = InterlockedIncrement(&successCount);
				if (c <= 4 || (c % 50) == 0)
					LOGF("derived-pso: ok=%ld call=%ld applied=%u/%u", c, call, applied, relevant);
				return hr;
			}
		}
	}
	else if (call <= 8)
	{
		LOGF("derived-pso: FALLBACK call=%ld descsOk=%d numDescs=%u streamBytes=%zu", call,
		     (int)descsOk, NumDescs, stream.size());
	}

	HRESULT hr = pSrcPipelineState->QueryInterface(riid, ppDerivedPipelineState);
	return hr;
}

HRESULT STDMETHODCALLTYPE XboxDevice::OpenExistingHeapFromAddress(const void* pAddress, REFIID riid,
                                                                void** ppvHeap)
{
	ID3D12Device3* d3 = RealAs<ID3D12Device3>(this, __uuidof(ID3D12Device3));
	if (!d3)
		return E_NOINTERFACE;
	HRESULT hr = d3->OpenExistingHeapFromAddress(pAddress, riid, ppvHeap);
	d3->Release();
	return hr;
}

HRESULT STDMETHODCALLTYPE XboxDevice::OpenExistingHeapFromFileMapping(HANDLE hFileMapping,
                                                                    REFIID riid, void** ppvHeap)
{
	ID3D12Device3* d3 = RealAs<ID3D12Device3>(this, __uuidof(ID3D12Device3));
	if (!d3)
		return E_NOINTERFACE;
	HRESULT hr = d3->OpenExistingHeapFromFileMapping(hFileMapping, riid, ppvHeap);
	d3->Release();
	return hr;
}

HRESULT STDMETHODCALLTYPE XboxDevice::RegisterPagePool2X(void*, void*, void*, void*)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE XboxDevice::EnqueueMakeResident(D3D12_RESIDENCY_FLAGS Flags, UINT NumObjects,
    ID3D12Pageable* const* ppObjects, ID3D12Fence* pFenceToSignal, UINT64 FenceValueToSignal)
{
	ID3D12Device3* d3 = RealAs<ID3D12Device3>(this, __uuidof(ID3D12Device3));
	if (!d3)
		return E_NOINTERFACE;
	HRESULT hr =
	    d3->EnqueueMakeResident(Flags, NumObjects, ppObjects, pFenceToSignal, FenceValueToSignal);
	d3->Release();
	return hr;
}

ID3D12Device* XboxDeviceCreate(ID3D12Device* real)
{
	return (ID3D12Device*)new XboxDevice(real);
}
