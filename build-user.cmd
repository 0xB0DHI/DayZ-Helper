@echo off
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT exit /b 1
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
if not exist dayz.ini copy /y dayz.ini.example dayz.ini >nul
if not exist out\imgui mkdir out\imgui
cl /nologo /std:c++17 /EHsc /W4 /WX /DNOMINMAX /c /Ithird_party\imgui /Ithird_party\imgui\backends /Foout\imgui\ third_party\imgui\imgui.cpp third_party\imgui\imgui_draw.cpp third_party\imgui\imgui_tables.cpp third_party\imgui\imgui_widgets.cpp third_party\imgui\backends\imgui_impl_win32.cpp third_party\imgui\backends\imgui_impl_dx11.cpp
if errorlevel 1 exit /b 1
set "IMGUI_OBJS=out\imgui\imgui.obj out\imgui\imgui_draw.obj out\imgui\imgui_tables.obj out\imgui\imgui_widgets.obj out\imgui\imgui_impl_win32.obj out\imgui\imgui_impl_dx11.obj"
set "COMMON=src\config\config.cpp src\data\placeholder_reader.cpp src\process\process.cpp"
cl /nologo /std:c++17 /EHsc /W4 /WX /DNOMINMAX /Ithird_party\imgui /Ithird_party\imgui\backends /Fe:out\dayz-game-tests.exe tests\game-tests.cpp %COMMON% /link version.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /DNOMINMAX /Ithird_party\imgui /Ithird_party\imgui\backends /Fe:out\dayz-controller.exe src\main.cpp src\app\app.cpp src\app\scene.cpp src\overlay\overlay.cpp %COMMON% %IMGUI_OBJS% /link user32.lib gdi32.lib d3d11.lib dxgi.lib dwmapi.lib version.lib /SUBSYSTEM:WINDOWS
exit /b %errorlevel%
