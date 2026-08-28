#pragma once
#include <windows.h>

namespace GDKScarlett::XGameRuntime
{
	using BrokerFn = HRESULT (*)(unsigned long long, unsigned long long, unsigned long long, unsigned long long);
	using QueryFn = HRESULT (*)(const GUID*, const GUID*, void**);
	using ReportFn = void (*)(unsigned long long, unsigned long long, unsigned long long, unsigned long long);
	using CanUnloadFn = HRESULT (*)();

	struct Broker
	{
		BrokerFn InitializeApiImpl;
		BrokerFn InitializeApiImplEx;
		BrokerFn InitializeApiImplEx2;
		BrokerFn UninitializeApiImpl;
		QueryFn QueryApiImpl;
		CanUnloadFn DllCanUnloadNow;
		ReportFn ErrorReport;
	};

	// Lazily loads the real System32 xgameruntime.dll and returns its resolved
	const Broker& RealBroker();
}
