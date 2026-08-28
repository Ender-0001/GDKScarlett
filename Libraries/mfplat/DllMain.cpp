#define GDKS_TRACE_TAG "mfplat"
#include "Common.h"
#include "RealMfplat.h"

using namespace GDKScarlett::Mfplat;

#define CALL(field, ...) \
	auto fn = RealMfplat().field; \
	return fn ? fn(__VA_ARGS__) : E_NOTIMPL

GDKS_EXPORT HRESULT MFResetDXGIDeviceManagerX(void*, void*, unsigned)
{
	TRACE();
	return S_OK;
}

GDKS_EXPORT HRESULT MFTEnumEx(GUID category, UINT32 flags, const void* inputType, const void* outputType, void*** activate, UINT32* count)
{
	CALL(mfTEnumEx, category, flags, inputType, outputType, activate, count);
}

GDKS_EXPORT HRESULT MFStartup(ULONG version, DWORD flags)
{
	CALL(mfStartup, version, flags);
}

GDKS_EXPORT HRESULT MFShutdown()
{
	CALL(mfShutdown);
}

GDKS_EXPORT HRESULT MFCreateMemoryBuffer(DWORD maxLength, void** buffer)
{
	CALL(mfCreateMemoryBuffer, maxLength, buffer);
}

GDKS_EXPORT HRESULT MFCreateDXGIDeviceManager(UINT* resetToken, void** manager)
{
	CALL(mfCreateDXGIDeviceManager, resetToken, manager);
}

GDKS_EXPORT HRESULT MFCreateAlignedMemoryBuffer(DWORD maxLength, DWORD alignment, void** buffer)
{
	CALL(mfCreateAlignedMemoryBuffer, maxLength, alignment, buffer);
}

GDKS_EXPORT HRESULT MFCreateSample(void** sample)
{
	CALL(mfCreateSample, sample);
}

GDKS_EXPORT HRESULT MFCreateMediaType(void** mediaType)
{
	CALL(mfCreateMediaType, mediaType);
}

GDKS_EXPORT HRESULT MFCreateWaveFormatExFromMFMediaType(void* mediaType, WAVEFORMATEX** format, UINT32* size, UINT32 flags)
{
	CALL(mfCreateWaveFormatExFromMFMediaType, mediaType, format, size, flags);
}

GDKS_EXPORT HRESULT MFPutWorkItem2(DWORD queue, LONG priority, void* callback, void* state)
{
	CALL(mfPutWorkItem2, queue, priority, callback, state);
}

GDKS_EXPORT HRESULT MFCreateAsyncResult(void* object, void* callback, void* state, void** result)
{
	CALL(mfCreateAsyncResult, object, callback, state, result);
}

GDKS_EXPORT HRESULT MFInvokeCallback(void* result)
{
	CALL(mfInvokeCallback, result);
}

GDKS_EXPORT HRESULT MFCreateAttributes(void** attributes, UINT32 initialSize)
{
	CALL(mfCreateAttributes, attributes, initialSize);
}

GDKS_EXPORT HRESULT MFCreateSourceResolver(void** resolver)
{
	CALL(mfCreateSourceResolver, resolver);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(module);
	}
	return TRUE;
}
