@echo off
rem Build (if needed) and run the convex-ue-codegen CLI, forwarding all args.
rem Usage: convex-ue-codegen.bat [codegen args...]   e.g. --help
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "ROOT=%SCRIPT_DIR%.."
set "BUILD=%ROOT%\build"

rem Prefer a multi-config (Visual Studio) layout, then a single-config one.
set "EXE=%BUILD%\cli\Release\convex-ue-codegen.exe"
if not exist "%EXE%" set "EXE=%BUILD%\cli\convex-ue-codegen.exe"

if not exist "%EXE%" (
    echo [convex-ue-codegen] first run: configuring and building...>&2
    cmake -S "%ROOT%" -B "%BUILD%" -DCONVEX_WITH_IXWEBSOCKET=ON -DCONVEX_UE_CODEGEN_BUILD_TESTS=OFF 1>&2
    if errorlevel 1 exit /b 1
    cmake --build "%BUILD%" --config Release --target convex-ue-codegen 1>&2
    if errorlevel 1 exit /b 1
    set "EXE=%BUILD%\cli\Release\convex-ue-codegen.exe"
    if not exist "!EXE!" set "EXE=%BUILD%\cli\convex-ue-codegen.exe"
)

"%EXE%" %*
