@echo off
setlocal
rem cod4map - build script.  Produces bin\cod4map.exe.
rem
rem /arch:IA32 and /fp:precise are required: the original is an x87 build and SSE2
rem code generation changes floating-point results, and therefore the output bytes.

set "ROOT=%~dp0"
set "OBJ=%ROOT%obj"
set "OUT=%ROOT%bin"
set "LOG=%OBJ%\build.log"
if not exist "%OBJ%" mkdir "%OBJ%"
if not exist "%OUT%" mkdir "%OUT%"

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR ( echo ERROR: Visual Studio not found & exit /b 1 )
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1

set "INC=/I. /I..\common /I..\src /I..\src\universal /I..\src\qcommon /I..\src\zlib /I..\src\d3dx /I..\libs\cmdlib"
set "CFLAGS=/nologo /c /O2 /arch:IA32 /fp:precise /GS- /EHsc /W3 /wd4996 /wd4244 /wd4018 /wd4267 /wd4305 /wd4101 /wd4700 /D_CRT_SECURE_NO_WARNINGS /DWIN32 /D_WINDOWS /DNDEBUG %EXTRA_CFLAGS%"

del /q "%LOG%" 2>nul
del /q "%OBJ%\*.obj" 2>nul

echo Compiling zlib as C...
pushd "%ROOT%src\zlib"
cl %CFLAGS% /TC /I. /Fo"%OBJ%\\" .\*.c >> "%LOG%" 2>&1
popd

echo Compiling cod4map as C++...
pushd "%ROOT%cod4map"
cl %CFLAGS% /TP %INC% /Fo"%OBJ%\\" .\*.cpp                  >> "%LOG%" 2>&1
cl %CFLAGS% /TP %INC% /Fo"%OBJ%\\" ..\common\*.cpp          >> "%LOG%" 2>&1
cl %CFLAGS% /TP %INC% /Fo"%OBJ%\\" ..\libs\cmdlib\*.cpp     >> "%LOG%" 2>&1
cl %CFLAGS% /TP %INC% /Fo"%OBJ%\\" ..\src\universal\*.cpp   >> "%LOG%" 2>&1
cl %CFLAGS% /TP %INC% /Fo"%OBJ%\\" ..\src\physics\ode\src\*.cpp >> "%LOG%" 2>&1
cl %CFLAGS% /TP %INC% /Fo"%OBJ%\\" ..\src\d3dx\*.cpp        >> "%LOG%" 2>&1
popd

echo Linking...
link /nologo /SUBSYSTEM:CONSOLE /STACK:0x400000,0x1000 /DYNAMICBASE:NO /FIXED /LARGEADDRESSAWARE ^
     /OUT:"%OUT%\cod4map.exe" "%OBJ%\*.obj" user32.lib gdi32.lib kernel32.lib advapi32.lib winmm.lib >> "%LOG%" 2>&1

if exist "%OUT%\cod4map.exe" (
  echo.
  echo Built %OUT%\cod4map.exe
) else (
  echo.
  echo BUILD FAILED - see %LOG%
  findstr /C:"error " "%LOG%"
  exit /b 1
)
