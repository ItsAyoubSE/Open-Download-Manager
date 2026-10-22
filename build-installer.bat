@echo off
setlocal
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 7\ISCC.exe"
if not exist "%ISCC%" set "ISCC=C:\Program Files\Inno Setup 7\ISCC.exe"
if not exist "%ISCC%" set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
  echo Inno Setup not found. Install it from https://jrsoftware.org/isdl.php
  exit /b 1
)

if not exist "odm.exe" (
  echo odm.exe not found - run build.bat first.
  exit /b 1
)

"%ISCC%" installer.iss
if errorlevel 1 (
  echo Installer build failed.
  exit /b 1
)
echo Installer written to dist\
