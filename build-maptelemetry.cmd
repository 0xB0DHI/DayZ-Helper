@echo off
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT exit /b 1
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
if not exist out mkdir out
if not exist out\maptelemetry mkdir out\maptelemetry
cl /nologo /TC /W0 /DNO_SSL /DNO_CGI /DUSE_WEBSOCKET /Ithird_party\civetweb /Foout\maptelemetry\civetweb.obj /c third_party\civetweb\civetweb.c
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /DNOMINMAX /Ithird_party\civetweb /Fe:out\maptelemetry-server.exe maptelemetry\server.cpp maptelemetry\server_core.cpp src\game\map_telemetry.cpp src\game\map_calibration.cpp out\maptelemetry\civetweb.obj /link ws2_32.lib bcrypt.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /DNOMINMAX /Fe:out\maptelemetry-tests.exe tests\maptelemetry-tests.cpp src\app\map_publisher.cpp maptelemetry\server_core.cpp src\game\map_telemetry.cpp src\game\map_calibration.cpp
exit /b %errorlevel%
