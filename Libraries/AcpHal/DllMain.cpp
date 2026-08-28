#include <windows.h>

#define GDKS_TRACE_TAG "AcpHal"
#include "Trace.h"

EXTERN_C HRESULT AcpHalCreate(void** out)
{
    TRACE();
    if (out) *out = (void*)1; // Non-Zero value
    return S_OK;
}

EXTERN_C HRESULT AcpHalAllocateShapeContexts(void*, void*, void*, void*)
{
    TRACE();
    return S_OK;
}

EXTERN_C HRESULT AcpHalReleaseShapeContexts(void*, void*, void*, void*)
{
    TRACE();
    return S_OK;
}

EXTERN_C HRESULT ApuCreateHeap(void*, void*, void*, void*)
{
    TRACE();
    return S_OK;
}

// Polled per audio frame on console
EXTERN_C HRESULT ApuHeapGetState(void*)
{
    TRACE_SAMPLED(4, 1000);
    return S_OK;
}

EXTERN_C HRESULT ApuAlloc(void*, void*, void*, void*)
{
    TRACE_SAMPLED(8, 1000);
    return S_OK;
}

EXTERN_C HRESULT ApuFree(void*, void*, void*, void*)
{
    TRACE_SAMPLED(8, 1000);
    return S_OK;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        LOGF("DLL_PROCESS_ATTACH");
    }
    return TRUE;
}
