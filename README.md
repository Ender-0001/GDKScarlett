# GDKScarlett

**Run Xbox GDK titles on desktop Windows.**

GDKScarlett is a compatibility layer for games built against the **Microsoft Game Development Kit
(GDK)** for Xbox One and Xbox Series X|S. It provides open reimplementations of the console-only
DLLs a GDK title links against, translating the Xbox graphics and system APIs to their desktop
Windows equivalents.

> **This is not an emulator.** Xbox One and Series consoles run x86-64, the same instruction
> set as a desktop PC. The game's own code executes natively on your CPU. There is no CPU
> emulation, no recompilation of game code, and no patching of the game binary.

---

## Name

The GDK is the toolchain introduced with the Xbox Series X|S, codename **Scarlett**, replacing
the older XDK. The name follows the toolchain.

Despite the name, the GDK isn't limited to Scarlett. It also supports Xbox One, codename
**Durango**, and Xbox One titles are fully in scope here.

The layer doesn't care which console a game was built for.

---

## Goals

- **Run unmodified GDK game binaries.** No patching the executable, no injected hooks, no
  engine-specific offsets.
- **Reimplement only the console-only surface.** Anything that already exists on desktop Windows
  is left alone.
- **Translate the Xbox D3D12 API faithfully**, rather than stubbing out what is inconvenient.
- **Run the game's own shaders**, including the native GPU microcode GDK titles ship instead of
  compilable IR.
- **Stay generic.** Nothing in the design is tied to a particular game or engine.

---

## Why most of it already works

A GDK title is an ordinary Windows PE. The Windows loader maps it and resolves its imports the
same way it would for any desktop application, and **the large majority of those imports
resolve against desktop Windows with no work at all.**

A GDK game typically imports the C runtime, `WS2_32`, `bcrypt`, `XAudio2_9`, `MFReadWrite`, and a
long list of `api-ms-win-*` and `ext-ms-win-*` API-set umbrellas. All of these exist on desktop
Windows and behave the same way. The game calls them and they simply work.

What remains is a short list of DLLs that ship only on console. Those are what this project
supplies, and they export exactly the symbols the game asks for. Nothing is hooked or detoured:
GDKScarlett *is* the DLL the game calls. The bootstrapper points the game at them without
touching the install (see [Running](#running)).

## What GDKScarlett implements

| DLL | Purpose | Approach |
|---|---|---|
| `d3d12_x.dll` | Xbox D3D12 (D3D12.X) graphics API | Reimplemented |
| `xmem.dll` | Console memory allocator | Reimplemented |
| `PIXEvt.dll` | PIX instrumentation and capture markers | Reimplemented (stub) |
| `AcpHal.dll` | Audio hardware abstraction layer | Reimplemented (stub) |
| `mfplat.dll` | Media Foundation, plus one Xbox-only export | Partial, forwards the rest |
| `xgameruntime.dll` | GDK runtime services broker | Partial, forwards the rest |

Supplied by the user, and used unmodified:

| DLL | Purpose |
|---|---|
| `xg_x.dll`, `xg_xs.dll` | Texture/resource layout computation |
| `dxcompiler_x.dll` | Shader compiler support |

These are Xbox console binaries and are not distributed with GDKScarlett. You must supply them
yourself.

### `d3d12_x`: the graphics layer

This is where nearly all of the work is. It covers:

- **Wrapper objects for the Xbox D3D12 interfaces.** Where the game asks for a device, queue,
  command list or resource, it receives an object implementing the Xbox interface whose methods
  either forward to a real desktop D3D12 object or translate the console-only ones.
- **Console-only API translation**: placed resources at explicit GPU addresses, page mappings,
  derived pipeline states, Xbox resource states and texture layouts, Xbox command-list and queue
  types, and indirect argument types with no desktop equivalent.
- **Present and the window.** GDK titles import no DXGI and never create a swapchain, because on
  console present happens inside the driver. GDKScarlett therefore owns the window and swapchain
  itself, implements the console present path on top of them, and provides the frame pacing the
  engine expects.
- **A shader recompiler.** GDK titles ship shaders as native GPU microcode with the compilable IR
  stripped out, so there is nothing for a desktop driver to consume. GDKScarlett decodes that
  microcode and rebuilds it into something desktop D3D12 can execute. See [Shaders](#shaders).

`xgameruntime.dll` is a broker: GDKScarlett implements the persistent-local-storage family and
forwards every other request to the real runtime in `System32`, so the parts of the GDK that do
exist on desktop are used rather than reimplemented.

`mfplat.dll` works the same way. Desktop Windows already has Media Foundation, but the console
build carries one extra export the game imports, so GDKScarlett supplies that export itself and
passes every other call through to the real `mfplat.dll` in `System32`.

Xbox Series titles that link `d3d12_xs.dll` are not supported yet; the bootstrapper reports it as
missing rather than starting a game that cannot run.

### Shaders

Each shader arrives as a container holding native GPU microcode with the compilable IR removed.
The layer rebuilds it in three steps:

1. **Decode.** The microcode is decoded instruction by instruction into a typed program.
2. **Reconstruct.** That program is emitted as HLSL, recovering structured control flow from the
   hardware's execution-mask idioms and mapping register-level resource reads onto constant
   buffers, structured buffers, textures and samplers.
3. **Compile.** The HLSL is compiled through `D3DCompile` from `d3dcompiler_47.dll` at Shader
   Model 5.1, and the result is substituted into the pipeline in place of the original.

Vertex, pixel and compute stages are substituted. Domain, hull and geometry stages are not, so a
pipeline carrying one falls back to a placeholder. Placeholders also cover any shader that fails
to decode or compile, which lets a pipeline degrade instead of failing to be created. A
placeholder pixel shader writes magenta, so an unsupported draw is obvious on screen.

Compilation happens at runtime on first use. Results are cached under
`%LOCALAPPDATA%\GDKScarlett\ShaderCache`, keyed by a hash of the microcode, so each shader is
translated once per machine. Cache filenames carry a version that is bumped whenever code
generation changes, so entries from an older translator are ignored rather than used.

### `xmem`

The console exposes a unified memory pool where CPU and GPU addresses coincide. Desktop memory is
split across upload, default and readback heaps over PCIe. `xmem` implements the console
allocation API on top of desktop virtual memory, and the layer bridges the model difference.

---

## Status

Early development. GDKScarlett boots titles and renders 2D content correctly; full 3D rendering is
the current focus. It is not yet playable, and there is no release.

## Building

Requires Windows 10/11 x64, a D3D12 GPU, and Visual Studio 2022 or later with the Desktop C++
workload.

Building produces the layer DLLs and `gdks.exe`, the bootstrapper.

## Running

`gdks.exe` starts the game and makes it load the layer instead of the console DLLs. The game
install is never modified: nothing is copied into it, and no file inside it is patched.

```
gdks <package-root> [--layer <dir>] [-- <game args>]
```

`<package-root>` is the directory holding `MicrosoftGame.config`. The bootstrapper reads that file
for the executable name, the package identity and the title ID, so the path to the game binary and
the location of the `G:\` mount are both derived from it rather than guessed.

`--layer` is where the layer DLLs live, defaulting to the current directory.

Anything after `--` is forwarded to the game unchanged.

Everything the game and the layer write with `OutputDebugString` is relayed to the `gdks`
console, so the game log and the D3D12 debug layer appear alongside the bootstrapper output
without attaching a debugger.

### How it works

The console DLLs a GDK title imports do not exist on desktop, and Windows resolves imports before
any code of ours can run. The bootstrapper works around that in four steps:

1. **Start suspended.** The process is created with `CREATE_SUSPENDED`, so the loader has not yet
   resolved a single import.
2. **Rewrite the import table.** Each console DLL the game imports is renamed in memory to its
   `gdks_` counterpart: `d3d12_x.dll` becomes `gdks_d3d12_x.dll`, and so on. The renamed strings
   go into a fresh block reachable from the image base, the import directory is repointed at it,
   and the bound-import directory is zeroed so the loader cannot use stale addresses.
3. **Preflight.** Before resuming, the bootstrapper checks every console import against the table
   of DLLs the layer provides and fails with a clear message if one is missing, rather than letting
   the loader fail obscurely.
4. **Resume.** The loader then resolves imports normally, finds the `gdks_` DLLs on the layer path,
   and the game runs against them.

The uniform `gdks_` prefix means every module the layer supplies is identifiable at a glance in a
debugger or module list, and a layer DLL can never be confused with a real console binary.

`xgameruntime.dll` is the exception: it keeps its original name, because it is resolved by name
rather than through the rewritten import table.

### Supplied vs. reimplemented

The bootstrapper distinguishes the DLLs GDKScarlett reimplements from the ones you must supply
yourself. `xg_x.dll`, `xg_xs.dll` and `dxcompiler_x.dll` are Microsoft binaries; they are loaded
under their original names, and nothing in this repository replaces them.

### Environment

The bootstrapper sets these for the child process; set them yourself to override.

| Variable | Meaning | Default |
| --- | --- | --- |
| `GDKS_G` | Target of the `G:\` mount (the package root) | From `MicrosoftGame.config` |
| `GDKS_T` | Target of the `T:\` mount (persistent local storage) | `%LOCALAPPDATA%` |
| `GDKS_D` | Target of the `D:\` mount (dev scratch) | `%LOCALAPPDATA%\GDKScarlett\Developer` |
| `GDKS_SHADERCACHE` | Translated shader cache | `%LOCALAPPDATA%\GDKScarlett\ShaderCache` |
| `GDKS_REDIRECT_RELATIVE` | Resolve relative paths against the package root | off |

Everything the layer writes (the shader cache and both emulated drives) lives under
`%LOCALAPPDATA%\GDKScarlett`, so the game directory stays untouched.

## Legal

GDKScarlett is an independent reimplementation of a console API surface, developed for
interoperability. It ships no Microsoft binaries and implements no content decryption or DRM
circumvention. You are responsible for supplying any Microsoft components required, and for
owning the software you run.

Xbox, GDK, and the Microsoft Game Development Kit are trademarks of Microsoft Corporation. This
project is not affiliated with, endorsed by, or sponsored by Microsoft.

## License

[BSD 2-Clause](LICENSE). Free to use, modify and redistribute, with attribution.
