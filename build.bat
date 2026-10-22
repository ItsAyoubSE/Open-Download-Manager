@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo Failed to initialize MSVC environment.
  exit /b 1
)
rc /nologo /fo app.res app.rc
if errorlevel 1 (
  echo Resource build failed.
  exit /b 1
)
cl /nologo /std:c++17 /EHsc /O2 /MT /utf-8 /DUNICODE /D_UNICODE main.cpp app.res ^
  /Fe:odm.exe ^
  /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup ^
  d2d1.lib dwrite.lib winhttp.lib ole32.lib shell32.lib user32.lib gdi32.lib dwmapi.lib comdlg32.lib advapi32.lib
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)
echo Build succeeded: odm.exe
