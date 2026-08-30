@echo off
setlocal enabledelayedexpansion

set ROOT=%~dp0
set VCPKG_INSTALLED=%ROOT%vcpkg\installed\x64-windows
set SPOUT_DIR=%ROOT%third_party\Spout
set BUILD_DIR=%ROOT%build

if not exist "%VCPKG_INSTALLED%\include\GLFW\glfw3.h" (
    echo [build] vcpkg dependencies not found. Run:
    echo   native\vcpkg\vcpkg.exe install glfw3 "imgui[core,glfw-binding,opengl3-binding]" yaml-cpp nlohmann-json --triplet x64-windows
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo [build] cl.exe not on PATH, locating vcvars64.bat via vswhere...
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set VSINSTALL=%%i
    )
    if not defined VSINSTALL (
        echo [build] Could not locate a Visual Studio install with the C++ toolset.
        exit /b 1
    )
    call "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
)

set SOURCES=
for %%D in (. artnet dmx serializers render spout config ui exporters generators) do (
    for %%f in ("%ROOT%src\%%D\*.cpp") do set SOURCES=!SOURCES! "%%f"
)

set SPOUT_SOURCES=
for %%f in ("%SPOUT_DIR%\*.cpp") do set SPOUT_SOURCES=!SPOUT_SOURCES! "%%f"

set INCLUDES=/I "%VCPKG_INSTALLED%\include" /I "%ROOT%src" /I "%ROOT%third_party\stb"
set DEFINES=/DGLFW_DLL /DYAML_CPP_DLL /D_CRT_SECURE_NO_WARNINGS
set LIBPATH=/LIBPATH:"%VCPKG_INSTALLED%\lib"
set LIBS=glfw3dll.lib imgui.lib yaml-cpp.lib opengl32.lib ws2_32.lib gdi32.lib user32.lib shell32.lib comdlg32.lib d3d11.lib dxgi.lib winmm.lib

echo [build] Compiling...
cl.exe /nologo /std:c++17 /EHsc /MD /W3 /O2 %DEFINES% %INCLUDES% ^
    !SOURCES! !SPOUT_SOURCES! ^
    /Fo"%BUILD_DIR%\\" /Fe"%BUILD_DIR%\HNode.exe" ^
    /link %LIBPATH% %LIBS% /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup

if errorlevel 1 (
    echo [build] Build FAILED.
    exit /b 1
)

echo [build] Copying runtime DLLs...
copy /Y "%VCPKG_INSTALLED%\bin\glfw3.dll" "%BUILD_DIR%\" >nul
copy /Y "%VCPKG_INSTALLED%\bin\yaml-cpp.dll" "%BUILD_DIR%\" >nul

echo [build] Done: %BUILD_DIR%\HNode.exe
