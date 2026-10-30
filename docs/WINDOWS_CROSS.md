# Cross-compiling a Windows build from Linux

This builds `MIRecompiled.exe` on Linux with clang-cl and Microsoft's CRT/Windows SDK (downloaded by
[xwin](https://github.com/Jake-Shadle/xwin)). Do step 1 of the normal build first (splat, N64Recomp, `scripts/recomp.sh`):
the recompiled C is the same for every platform.

Tested with Clang 20 and Wine 9.0 (Ubuntu 24.04: `clang-20 lld-20 llvm-20 wine64`) and Windows SDK 10.0.26100. The
result was played on Windows 11 with an RTX 5090 (D3D12).

Requirements:

- **Clang 19 or newer.** The current MSVC STL refuses older Clang.
- **Wine.** The D3D12 shaders (DXIL) are compiled with RT64's bundled Windows `dxc.exe`, run through
  `lib/rt64/src/tools/dxc-wine.sh`. They must come from the same DXC as the `dxcompiler.dll` shipped next to the exe.
  RT64 links its precompiled library shaders (`RasterVSLibrary`, `RasterPSLibrary`) with shaders it compiles at
  runtime, and D3D12 refuses to link DXIL from different compiler versions ("Cannot link libraries with conflicting
  compiler versions"). The bundled `dxc-linux` is 1.8 and the bundled Windows DXC is 1.7, so a cross-build that used
  `dxc-linux` for DXIL crashed as soon as the game started. SPIR-V still uses `dxc-linux`.

```sh
# Microsoft CRT and Windows SDK (accepting Microsoft's license)
cargo install --locked xwin
xwin --accept-license --arch x86_64 splat --output ~/xwin

# Linux paths are case-sensitive; a few includes/libs in the dependencies use different casing than the SDK
I=~/xwin/sdk/include
ln -sf ShlObj.h $I/um/Shlobj.h; ln -sf ShellScalingApi.h $I/um/ShellScalingAPI.h
ln -sf memory.h $I/ucrt/Memory.h; ln -sf math.h $I/ucrt/Math.h
L=~/xwin/sdk/lib/um/x86_64
ln -sf User32.Lib $L/User32.lib; ln -sf shcore.lib $L/Shcore.lib; ln -sf WinMM.Lib $L/Winmm.lib; ln -sf ShLwApi.Lib $L/Shlwapi.lib

# file_to_c runs during the build, so it has to be a Linux binary
mkdir -p build/host && clang++ -O2 -std=c++17 lib/rt64/src/tools/file_to_c/file_to_c.cpp -o build/host/file_to_c

cmake -S . -B build/win -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/windows-cross-clang-cl.cmake -DXWIN_DIR=$HOME/xwin -DLLVM_BIN_DIR=/usr/lib/llvm-20/bin \
  -DRT64_HOST_FILE_TO_C=$PWD/build/host/file_to_c
cmake --build build/win
```

`build/win` then holds `MIRecompiled.exe`, `SDL2.dll`, `dxcompiler.dll` and `dxil.dll`. Ship those with `assets/` and
`recompcontrollerdb.txt`, which are symlinks in the build dir, so copy the real files. The exe needs the Visual C++
2015-2022 x64 runtime.

To smoke-test without Windows: Wine plus lavapipe (`mesa-vulkan-drivers`) reaches the title screen through Vulkan.
Wine needs an audio device (e.g. a PulseAudio null sink), because startup fails without one. That path doesn't
exercise D3D12's shader linking, so test D3D12 on real Windows.
