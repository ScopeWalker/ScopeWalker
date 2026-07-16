@echo off
REM ============================================================
REM  ScopeWalker - build script (MSVC / Visual Studio)
REM  Double-click this file, or run it from a terminal.
REM  It locates Visual Studio automatically and compiles
REM  src\scopewalker.c into scopewalker.exe next to this file.
REM ============================================================

setlocal
cd /d "%~dp0"

echo.
echo === Locating Visual Studio ===
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found. Is Visual Studio ^(or Build Tools^) installed?
    goto :fail
)

REM Ask vswhere for the newest install that has the C++ toolset.
set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"

if not defined VSPATH (
    echo [ERROR] No Visual Studio C++ toolset found.
    goto :fail
)

set "VCVARS=%VSPATH%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found under "%VSPATH%".
    goto :fail
)

echo Found: %VSPATH%
echo.
echo === Setting up the x64 build environment ===
call "%VCVARS%" >nul

echo.
echo === Compiling scopewalker.exe ===
cl /nologo /O2 /W3 src\scopewalker.c src\core\draw.c ^
   /link gdi32.lib user32.lib dwmapi.lib advapi32.lib ^
   /subsystem:windows /out:scopewalker.exe

if errorlevel 1 goto :fail

REM Clean up the intermediate object files.
del *.obj 2>nul

echo.
echo === Build succeeded: scopewalker.exe ===
goto :done

:fail
echo.
echo *** BUILD FAILED ***

:done
echo.
pause
endlocal
