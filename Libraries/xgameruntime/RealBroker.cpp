#include "RealBroker.h"

#include <mutex>

namespace GDKScarlett::XGameRuntime
{
	static Broker GBroker{};
	static HMODULE GReal = nullptr;

	static void Load()
	{
		wchar_t systemPath[MAX_PATH];
		UINT length = GetSystemDirectoryW(systemPath, MAX_PATH);
		if (length == 0 || length > MAX_PATH - 20)
		{
			return;
		}
		lstrcatW(systemPath, L"\\xgameruntime.dll");

		GReal = LoadLibraryW(systemPath);
		if (!GReal)
		{
			return;
		}

		GBroker.InitializeApiImpl = (BrokerFn)GetProcAddress(GReal, "InitializeApiImpl");
		GBroker.InitializeApiImplEx = (BrokerFn)GetProcAddress(GReal, "InitializeApiImplEx");
		GBroker.InitializeApiImplEx2 = (BrokerFn)GetProcAddress(GReal, "InitializeApiImplEx2");
		GBroker.UninitializeApiImpl = (BrokerFn)GetProcAddress(GReal, "UninitializeApiImpl");
		GBroker.QueryApiImpl = (QueryFn)GetProcAddress(GReal, "QueryApiImpl");
		GBroker.DllCanUnloadNow = (CanUnloadFn)GetProcAddress(GReal, "DllCanUnloadNow");
		GBroker.ErrorReport = (ReportFn)GetProcAddress(GReal, "XErrorReport");
	}

	const Broker& RealBroker()
	{
		static std::once_flag once;
		std::call_once(once, Load);
		return GBroker;
	}
}
