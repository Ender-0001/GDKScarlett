#define GDKS_TRACE_TAG "mfplat"
#include "Common.h"
#include "RealMfplat.h"

#include <mutex>

namespace GDKScarlett::Mfplat
{
	static Real GReal{};
	static HMODULE GModule = nullptr;

	static void Load()
	{
		wchar_t systemPath[MAX_PATH];
		UINT length = GetSystemDirectoryW(systemPath, MAX_PATH);
		if (length == 0 || length > MAX_PATH - 16)
		{
			return;
		}
		lstrcatW(systemPath, L"\\mfplat.dll");

		GModule = LoadLibraryW(systemPath);
		if (!GModule)
		{
			LOGF("could not load %ls (%lu)", systemPath, GetLastError());
			return;
		}

		#define RESOLVE(field, name) GReal.field = decltype(GReal.field)(GetProcAddress(GModule, name))
		RESOLVE(mfTEnumEx, "MFTEnumEx");
		RESOLVE(mfStartup, "MFStartup");
		RESOLVE(mfShutdown, "MFShutdown");
		RESOLVE(mfCreateMemoryBuffer, "MFCreateMemoryBuffer");
		RESOLVE(mfCreateDXGIDeviceManager, "MFCreateDXGIDeviceManager");
		RESOLVE(mfCreateAlignedMemoryBuffer, "MFCreateAlignedMemoryBuffer");
		RESOLVE(mfCreateSample, "MFCreateSample");
		RESOLVE(mfCreateMediaType, "MFCreateMediaType");
		RESOLVE(mfCreateWaveFormatExFromMFMediaType, "MFCreateWaveFormatExFromMFMediaType");
		RESOLVE(mfPutWorkItem2, "MFPutWorkItem2");
		RESOLVE(mfCreateAsyncResult, "MFCreateAsyncResult");
		RESOLVE(mfInvokeCallback, "MFInvokeCallback");
		RESOLVE(mfCreateAttributes, "MFCreateAttributes");
		RESOLVE(mfCreateSourceResolver, "MFCreateSourceResolver");
		#undef RESOLVE
	}

	const Real& RealMfplat()
	{
		static std::once_flag once;
		std::call_once(once, Load);
		return GReal;
	}
}
