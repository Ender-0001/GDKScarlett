#pragma once
#include <windows.h>

namespace GDKScarlett::Log
{
	void Write(const char* tag, const char* text);
	void WriteFormat(const char* tag, const char* format, ...);

	void SetSink(void (*sink)(const char* line));
}
