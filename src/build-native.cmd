@echo off
setlocal
if /I "%VSCMD_ARG_TGT_ARCH%"=="x64" goto compiler_ready
set "guardVsWhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%guardVsWhere%" (
  echo Visual Studio 2022 C++ tools not found. Install the Desktop development with C++ workload.
  exit /b 1
)
set "guardVsRoot="
pushd "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if errorlevel 1 exit /b 1
for /f "usebackq delims=" %%I in (`vswhere.exe -latest -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "guardVsRoot=%%I"
popd
if not defined guardVsRoot (
  echo Visual Studio 2022 x64 C++ tools not found.
  exit /b 1
)
call "%guardVsRoot%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
:compiler_ready
pushd "%~dp0"
set "guardOutput=..\ChainedGuard.Native.dll"
if not "%~1"=="" set "guardOutput=%~1"
if not exist build mkdir build
cl /nologo /O2 /MT /std:c++17 /utf-8 /EHsc /LD /Fobuild\ /Ivendor\minhook\include Native.cpp vendor\minhook\src\buffer.c vendor\minhook\src\hook.c vendor\minhook\src\trampoline.c vendor\minhook\src\hde\hde64.c /link /OUT:"%guardOutput%" user32.lib
set "guardResult=%errorlevel%"
popd
exit /b %guardResult%
