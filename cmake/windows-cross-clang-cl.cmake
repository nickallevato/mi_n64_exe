# Cross-compile a Windows x64 build from Linux with clang-cl and an MSVC CRT/Windows SDK downloaded by xwin.
# Needs Clang 19 or newer (the current MSVC STL rejects older Clang) and Wine (for DXIL shaders). See docs/WINDOWS_CROSS.md.
#
#   cmake -S . -B build/win -G Ninja -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/windows-cross-clang-cl.cmake -DXWIN_DIR=$HOME/xwin \
#     -DRT64_HOST_FILE_TO_C=$PWD/build/host/file_to_c
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(XWIN_DIR "$ENV{XWIN_DIR}" CACHE PATH "Output directory of 'xwin splat'")
if (NOT XWIN_DIR)
    message(FATAL_ERROR "Set XWIN_DIR to the output directory of 'xwin --accept-license splat --output <dir>'")
endif()
set(LLVM_BIN_DIR "" CACHE PATH "Directory with clang-cl, lld-link, llvm-lib, llvm-rc and llvm-mt (default: PATH)")
# The toolchain file is re-read for CMake's try_compile checks, which only see variables listed here.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES XWIN_DIR LLVM_BIN_DIR)

find_program(CMAKE_C_COMPILER clang-cl HINTS ${LLVM_BIN_DIR} REQUIRED)
find_program(CMAKE_CXX_COMPILER clang-cl HINTS ${LLVM_BIN_DIR} REQUIRED)
find_program(CMAKE_LINKER lld-link HINTS ${LLVM_BIN_DIR} REQUIRED)
find_program(CMAKE_AR llvm-lib HINTS ${LLVM_BIN_DIR} REQUIRED)
find_program(CMAKE_RC_COMPILER llvm-rc HINTS ${LLVM_BIN_DIR} REQUIRED)
find_program(CMAKE_MT llvm-mt HINTS ${LLVM_BIN_DIR} REQUIRED)

set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

set(_xwin_inc "/imsvc${XWIN_DIR}/crt/include /imsvc${XWIN_DIR}/sdk/include/ucrt /imsvc${XWIN_DIR}/sdk/include/um /imsvc${XWIN_DIR}/sdk/include/shared /imsvc${XWIN_DIR}/sdk/include/winrt")
set(CMAKE_C_FLAGS_INIT "${_xwin_inc} -fms-compatibility-version=19.43")
set(CMAKE_CXX_FLAGS_INIT "${_xwin_inc} -fms-compatibility-version=19.43 /EHsc")
set(CMAKE_RC_FLAGS_INIT "-I${XWIN_DIR}/sdk/include/um -I${XWIN_DIR}/sdk/include/shared")

set(_xwin_lib "/libpath:${XWIN_DIR}/crt/lib/x86_64 /libpath:${XWIN_DIR}/sdk/lib/um/x86_64 /libpath:${XWIN_DIR}/sdk/lib/ucrt/x86_64")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_xwin_lib}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_xwin_lib}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_xwin_lib}")

# xwin only ships release CRT libraries.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
