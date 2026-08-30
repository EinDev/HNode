@echo off
setlocal enabledelayedexpansion

rem Builds the CPU perf-baseline benchmark (native/src/bench/BenchMain.cpp).
rem Deliberately has ZERO external dependencies (no vcpkg, no GLFW/ImGui/Spout) - it
rem only links DmxBuffer.cpp and VrslSerializer.cpp directly, so this is fast and
rem needs no `native\vcpkg\vcpkg.exe install` step first, unlike build.bat.

set ROOT=%~dp0
set BUILD_DIR=%ROOT%build

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo [bench] cl.exe not on PATH, locating vcvars64.bat via vswhere...
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set VSINSTALL=%%i
    )
    if not defined VSINSTALL (
        echo [bench] Could not locate a Visual Studio install with the C++ toolset.
        exit /b 1
    )
    call "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
)

echo [bench] Compiling (no external dependencies needed)...
cl.exe /nologo /std:c++17 /EHsc /MD /W3 /O2 ^
    "%ROOT%src\bench\BenchMain.cpp" "%ROOT%src\bench\VrslSerializerUiStub.cpp" ^
    "%ROOT%src\dmx\DmxBuffer.cpp" "%ROOT%src\serializers\VrslSerializer.cpp" ^
    /Fo"%BUILD_DIR%\\" /Fe"%BUILD_DIR%\bench.exe"

if errorlevel 1 (
    echo [bench] Build FAILED.
    exit /b 1
)

echo [bench] Done: %BUILD_DIR%\bench.exe
