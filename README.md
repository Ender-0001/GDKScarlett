# GDKScarlett

A compatibility layer for running Xbox GDK titles on desktop Windows.

Games built with the Microsoft Game Development Kit (GDK) for Xbox One and Xbox Series X|S link
against a handful of DLLs that only exist on console. GDKScarlett provides open reimplementations
of those DLLs, translating the Xbox graphics and system APIs to their desktop Windows equivalents.

It is not an emulator. Xbox One and Series consoles are x86-64, so the game's code runs natively on
your CPU. Nothing is emulated or recompiled, and the game binary is never patched.

## Status

Early development. GDKScarlett boots titles and renders 2D content correctly, and full 3D rendering
is the current focus. Nothing is playable yet, and there's no release.

Xbox Series titles that link `d3d12_xs.dll` aren't supported. The bootstrapper reports the DLL as
missing instead of launching the game.

## Goals

- Run unmodified GDK binaries: no executable patches, injected hooks, or engine-specific offsets.
- Replace only what's missing on desktop. Anything Windows already provides is left alone.
- Translate Xbox D3D12 faithfully instead of stubbing out the hard parts.
- Run the game's own shaders, which GDK titles ship as native GPU microcode.
- Stay generic. Nothing is tied to a particular game or engine.

## Architecture

A GDK title is an ordinary Windows PE, and most of its imports resolve against desktop Windows
as-is: the C runtime, `WS2_32`, `bcrypt`, `XAudio2_9`, `MFReadWrite`, and many `api-ms-win-*` and
`ext-ms-win-*` API sets.

The rest are console-only DLLs. GDKScarlett supplies these, exporting exactly the symbols the game
imports. There's no hooking or detouring; the game calls GDKScarlett's DLLs directly. The
bootstrapper redirects the game to them without modifying the install (see [Running](#running)).

### Components

| DLL | Purpose | Implementation |
|---|---|---|
| `d3d12_x.dll` | Xbox D3D12 (D3D12.X) graphics API | Full |
| `xmem.dll` | Console memory allocator | Full |
| `PIXEvt.dll` | PIX instrumentation and capture markers | Stub |
| `AcpHal.dll` | Audio hardware abstraction layer | Stub |
| `mfplat.dll` | Media Foundation, plus one Xbox-only export | Partial, forwards the rest |
| `xgameruntime.dll` | GDK runtime services broker | Partial, forwards the rest |

These Microsoft binaries are also required. They're used unmodified and aren't distributed with
GDKScarlett, so you need to supply them yourself:

| DLL | Purpose |
|---|---|
| `xg_x.dll`, `xg_xs.dll` | Texture and resource layout computation |
| `dxcompiler_x.dll` | Shader compiler support |

### Shaders

GDK titles ship shaders as native GPU microcode with the compilable IR stripped out, so there's
nothing a desktop driver can consume. GDKScarlett rebuilds each one in three steps:

1. Decode the microcode into a typed program.
2. Emit HLSL, recovering structured control flow from the hardware's execution-mask idioms and
   mapping register-level resource reads to constant buffers, structured buffers, textures and
   samplers.
3. Compile the HLSL with `D3DCompile` (`d3dcompiler_47.dll`) at Shader Model 5.1 and substitute it
   into the pipeline.

Vertex, pixel and compute shaders are translated. Hull, domain and geometry shaders aren't, so
pipelines that use them get a placeholder. The same goes for any shader that fails to decode or
compile, so the pipeline is still created. Placeholder pixel shaders output magenta, which makes
unsupported draws easy to spot.

Shaders are translated on first use and cached in `%LOCALAPPDATA%\GDKScarlett\ShaderCache`, keyed
by a hash of the microcode. Cache filenames include a translator version, so changes to code
generation invalidate old entries.

## Building

Requirements:

- Windows 10 or 11, x64
- A D3D12-capable GPU
- Visual Studio 2022 or later with the Desktop development with C++ workload

The build produces the layer DLLs and `gdks.exe`, the bootstrapper.

## Running

```
gdks <package-root> [--layer <dir>] [-- <game args>]
```

- `<package-root>` is the directory containing `MicrosoftGame.config`. `gdks` reads the executable
  name, package identity and title ID from it, and uses them to locate the game binary and set up
  the `G:\` mount.
- `--layer <dir>` is where the layer DLLs are. Defaults to the current directory.
- Arguments after `--` are passed to the game.

`gdks` relays everything the game and layer write with `OutputDebugString` to its console, so the
game log and D3D12 debug layer output show up without attaching a debugger.

### How it works

Windows resolves imports before any of our code runs, and the console DLLs don't exist on
desktop. So `gdks`:

1. Creates the game process with `CREATE_SUSPENDED`, before the loader has resolved any imports.
2. Rewrites the import table in memory, renaming each console DLL with a `gdks_` prefix
   (`d3d12_x.dll` becomes `gdks_d3d12_x.dll`). The new names go in a fresh block reachable from the
   image base, the import directory is repointed at it, and the bound-import directory is zeroed
   so the loader can't use stale addresses.
3. Checks every console import against the DLLs the layer provides, and exits with an error naming
   any that are missing.
4. Resumes the process. The loader finds the `gdks_` DLLs on the layer path and resolves imports as
   usual.

The prefix makes layer DLLs easy to identify in a debugger or module list.

Some DLLs keep their original names: `xgameruntime.dll`, which is resolved by name rather than
through the import table, and the Microsoft-supplied `xg_x.dll`, `xg_xs.dll` and
`dxcompiler_x.dll`.

### Environment

`gdks` sets these for the game process. Set them yourself to override the defaults.

| Variable | Meaning | Default |
| --- | --- | --- |
| `GDKS_G` | Target of the `G:\` mount (package root) | From `MicrosoftGame.config` |
| `GDKS_T` | Target of the `T:\` mount (persistent local storage) | `%LOCALAPPDATA%` |
| `GDKS_D` | Target of the `D:\` mount (dev scratch) | `%LOCALAPPDATA%\GDKScarlett\Developer` |
| `GDKS_SHADERCACHE` | Translated shader cache | `%LOCALAPPDATA%\GDKScarlett\ShaderCache` |
| `GDKS_REDIRECT_RELATIVE` | Resolve relative paths against the package root | Off |

## Legal

GDKScarlett is an independent reimplementation of a console API surface, built for
interoperability. It includes no Microsoft binaries and no decryption or DRM circumvention. You're
responsible for supplying the required Microsoft components and for owning the software you run.

Xbox, GDK and Microsoft Game Development Kit are trademarks of Microsoft Corporation. This project
is not affiliated with, endorsed by, or sponsored by Microsoft.

## License

[BSD 2-Clause](LICENSE).
