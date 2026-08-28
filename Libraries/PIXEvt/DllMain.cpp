#include <windows.h>

#define GDKS_TRACE_TAG "PIXEvt"
#include "Trace.h"

EXTERN_C HRESULT PIXBeginCapture2(unsigned, const void*)
{
    TRACE();
    return S_OK;
}

EXTERN_C HRESULT PIXEndCapture(BOOL)
{
    TRACE();
    return S_OK;
}

EXTERN_C void* PIXEventsReplaceBlock(void*, BOOL)
{
    TRACE();
    return nullptr;
}

EXTERN_C unsigned long PIXGetCaptureState()
{
    TRACE_N(3);
    return 0;
}

EXTERN_C unsigned long PIXRecordMemoryAllocationEvent()
{
    TRACE_N(3);
    return 0;
}

EXTERN_C unsigned long PIXRecordMemoryFreeEvent()
{
    TRACE_N(3);
    return 0;
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
