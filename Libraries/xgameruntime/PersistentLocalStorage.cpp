#include "PersistentLocalStorage.h"

#include <cstdint>

namespace GDKScarlett::XGameRuntime
{
	const GUID PersistentLocalStorageApiFamily =
		{ 0xf4faf4d4, 0x2d04, 0x4fce, { 0xb3, 0xe0, 0x47, 0x4a, 0x71, 0x3a, 0x3e, 0x84 } };

	namespace
	{
		// T:\ is the console persistent-storage drive, which xmem maps to a real
		const char GStorageRoot[] = "T:\\";

		struct XPersistentLocalStorageSpaceInfo
		{
			uint64_t availableFreeSpace;
			uint64_t totalFreeSpace;
			uint64_t totalQuota;
		};

		struct IUnknownGdk
		{
			virtual HRESULT STDMETHODCALLTYPE QueryInterface(const GUID* iid, void** out) = 0;
			virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
			virtual ULONG STDMETHODCALLTYPE Release() = 0;
		};

		struct IPersistentLocalStorageImpl1 : IUnknownGdk
		{
			virtual HRESULT STDMETHODCALLTYPE GetPathSize(uint64_t* pathSize) = 0;
			virtual HRESULT STDMETHODCALLTYPE GetPath(uint64_t pathSize, char* path, uint64_t* pathUsed) = 0;
		};

		struct IPersistentLocalStorageImpl2 : IPersistentLocalStorageImpl1
		{
			virtual HRESULT STDMETHODCALLTYPE GetSpaceInfo(XPersistentLocalStorageSpaceInfo* info) = 0;
			virtual HRESULT STDMETHODCALLTYPE PromptUserForSpaceAsync(uint64_t bytes, void* asyncBlock) = 0;
			virtual HRESULT STDMETHODCALLTYPE PromptUserForSpaceResult(void* asyncBlock) = 0;
		};

		struct PersistentLocalStorage final : IPersistentLocalStorageImpl2
		{
			HRESULT STDMETHODCALLTYPE QueryInterface(const GUID*, void** out) override
			{
				if (out)
				{
					*out = this;
				}
				return S_OK;
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return 1;
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				return 1;
			}

			HRESULT STDMETHODCALLTYPE GetPathSize(uint64_t* pathSize) override
			{
				if (!pathSize)
				{
					return E_INVALIDARG;
				}
				*pathSize = sizeof(GStorageRoot);   // includes the null terminator
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GetPath(uint64_t pathSize, char* path, uint64_t* pathUsed) override
			{
				if (!path || pathSize < sizeof(GStorageRoot))
				{
					return E_INVALIDARG;
				}
				memcpy(path, GStorageRoot, sizeof(GStorageRoot));
				if (pathUsed)
				{
					*pathUsed = sizeof(GStorageRoot);
				}
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GetSpaceInfo(XPersistentLocalStorageSpaceInfo* info) override
			{
				if (!info)
				{
					return E_INVALIDARG;
				}
				const uint64_t space = 64ull * 1024 * 1024 * 1024;
				info->availableFreeSpace = space;
				info->totalFreeSpace = space;
				info->totalQuota = space;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE PromptUserForSpaceAsync(uint64_t, void*) override
			{
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE PromptUserForSpaceResult(void*) override
			{
				return S_OK;
			}
		};

		PersistentLocalStorage GInstance;
	}

	void* PersistentLocalStorageInterface()
	{
		return static_cast<IPersistentLocalStorageImpl2*>(&GInstance);
	}
}
