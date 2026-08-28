#pragma once
#include <windows.h>

namespace GDKScarlett::XGameRuntime
{
	void InstallPathHooks(HMODULE self);
	void RemovePathHooks();
}
