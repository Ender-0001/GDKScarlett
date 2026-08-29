#pragma once

#include <d3d12.h>

namespace GDKScarlett::D3D12X
{
	// stageType is the PSO subobject type: 1=VS 2=PS 3=DS 4=HS 5=GS 6=CS.
	bool TryRecompileToDxil(UINT stageType, bool hasRenderTarget,
	                        const D3D12_SHADER_BYTECODE* input, D3D12_SHADER_BYTECODE* output,
	                        bool allowCache = true);

	struct RecompilerTimings
	{
		double locateMs, cacheIoMs, compileMs;
		long locateCalls, locateMemoHits, cacheIoCalls, compileCalls, aliasHits, scanAttempts;
		double translateMs, aliasIoMs, linkFixMs;
		double maxLocateMs, maxCompileMs, maxTranslateMs;
		long translateCalls, aliasIoCalls, linkFixCalls;
		long long scanInstructions;
	};
	void GetRecompilerTimings(RecompilerTimings* out);
	void LogRecompilerStats();

	bool HasCacheEntry(const D3D12_SHADER_BYTECODE* input);

	bool TryLinkFixVs(const D3D12_SHADER_BYTECODE* vs, const D3D12_SHADER_BYTECODE* ps,
	                  const D3D12_INPUT_LAYOUT_DESC* inputLayout, D3D12_SHADER_BYTECODE* output);

	void BeginPsoKeyCapture();
	const char* PsoKeyCapture();
}
