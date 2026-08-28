#define GDKS_TRACE_TAG "game"
#include "Common.h"
#include "DebugOutput.h"

#include <stdio.h>

struct DebugBuffer
{
	DWORD processId;
	char text[4096 - sizeof(DWORD)];
};

static HANDLE GMutex = nullptr;
static HANDLE GMapping = nullptr;
static HANDLE GBufferReady = nullptr;
static HANDLE GDataReady = nullptr;
static HANDLE GStop = nullptr;
static HANDLE GThread = nullptr;
static DebugBuffer* GBuffer = nullptr;
static DWORD GProcessId = 0;

static void Emit(const char* text)
{
	char line[sizeof(GBuffer->text)];
	size_t length = 0;
	while (length + 1 < sizeof(line) && text[length])
	{
		line[length] = text[length];
		++length;
	}
	while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
	{
		--length;
	}
	line[length] = 0;
	if (length)
	{
		printf("[%s] %s\n", GDKS_TRACE_TAG, line);
		fflush(stdout);
	}
}

static DWORD WINAPI Relay(LPVOID)
{
	HANDLE waits[2] = { GDataReady, GStop };
	for (;;)
	{
		SetEvent(GBufferReady);
		DWORD signalled = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
		if (signalled != WAIT_OBJECT_0)
		{
			return 0;
		}
		if (GBuffer->processId == GProcessId)
		{
			Emit(GBuffer->text);
		}
	}
}

void StartDebugOutputRelay(DWORD processId)
{
	GProcessId = processId;

	GMutex = CreateMutexW(nullptr, FALSE, L"DBWinMutex");
	GBufferReady = CreateEventW(nullptr, FALSE, TRUE, L"DBWIN_BUFFER_READY");
	GDataReady = CreateEventW(nullptr, FALSE, FALSE, L"DBWIN_DATA_READY");
	GMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
	                              sizeof(DebugBuffer), L"DBWIN_BUFFER");
	if (!GBufferReady || !GDataReady || !GMapping)
	{
		LOGF("debug output unavailable (%lu)", GetLastError());
		StopDebugOutputRelay();
		return;
	}
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		LOGF("another debug output monitor is running; lines may go to it instead");
	}

	GBuffer = (DebugBuffer*)MapViewOfFile(GMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
	                                      sizeof(DebugBuffer));
	if (!GBuffer)
	{
		LOGF("could not map the debug output buffer (%lu)", GetLastError());
		StopDebugOutputRelay();
		return;
	}

	GStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	GThread = CreateThread(nullptr, 0, Relay, nullptr, 0, nullptr);
	if (!GThread)
	{
		LOGF("could not start the debug output relay (%lu)", GetLastError());
		StopDebugOutputRelay();
	}
}

void StopDebugOutputRelay()
{
	if (GThread)
	{
		SetEvent(GStop);
		WaitForSingleObject(GThread, 2000);
		CloseHandle(GThread);
		GThread = nullptr;
	}
	if (GBuffer)
	{
		UnmapViewOfFile(GBuffer);
		GBuffer = nullptr;
	}
	HANDLE* handles[] = { &GStop, &GMapping, &GDataReady, &GBufferReady, &GMutex };
	for (HANDLE* handle : handles)
	{
		if (*handle)
		{
			CloseHandle(*handle);
			*handle = nullptr;
		}
	}
}
