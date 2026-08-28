#include <windows.h>

#include "PathHooks.h"
#include "PersistentLocalStorage.h"
#include "RealBroker.h"

using namespace GDKScarlett::XGameRuntime;

using Arg = unsigned long long;

#define CALL(field, ...) \
	auto fn = RealBroker().field; \
	return fn ? fn(__VA_ARGS__) : E_NOTIMPL

EXTERN_C HRESULT InitializeApiImpl(Arg a0, Arg a1, Arg a2, Arg a3)
{
	CALL(InitializeApiImpl, a0, a1, a2, a3);
}

EXTERN_C HRESULT InitializeApiImplEx(Arg a0, Arg a1, Arg a2, Arg a3)
{
	CALL(InitializeApiImplEx, a0, a1, a2, a3);
}

EXTERN_C HRESULT InitializeApiImplEx2(Arg a0, Arg a1, Arg a2, Arg a3)
{
	CALL(InitializeApiImplEx2, a0, a1, a2, a3);
}

EXTERN_C HRESULT QueryApiImpl(const GUID* apiFamily, const GUID* interfaceId, void** ppInterface)
{
	if (apiFamily && IsEqualGUID(*apiFamily, PersistentLocalStorageApiFamily) && ppInterface)
	{
		*ppInterface = PersistentLocalStorageInterface();
		return S_OK;
	}
	CALL(QueryApiImpl, apiFamily, interfaceId, ppInterface);
}

EXTERN_C HRESULT UninitializeApiImpl(Arg a0, Arg a1, Arg a2, Arg a3)
{
	CALL(UninitializeApiImpl, a0, a1, a2, a3);
}

EXTERN_C HRESULT DllCanUnloadNow()
{
	CALL(DllCanUnloadNow);
}

EXTERN_C void XErrorReport(Arg a0, Arg a1, Arg a2, Arg a3)
{
	ReportFn fn = RealBroker().ErrorReport;
	if (fn)
	{
		fn(a0, a1, a2, a3);
	}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		InstallPathHooks(module);
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		RemovePathHooks();
	}
	return TRUE;
}
