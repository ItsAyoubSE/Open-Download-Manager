@echo off
rem Sign odm.exe with an Authenticode certificate.
rem
rem   set ODM_PFX=C:\path\to\cert.pfx
rem   set ODM_PFX_PASS=yourpassword
rem   sign.bat
rem
rem For a real release use a CA-issued code signing certificate
rem (OV/EV). This also works with a self-signed certificate, but that
rem only removes warnings on machines that trust that certificate.
setlocal
if "%ODM_PFX%"=="" (
  echo ODM_PFX is not set. Point it at your .pfx file.
  exit /b 1
)

set "SIGNTOOL="
where signtool >nul 2>nul && set "SIGNTOOL=signtool"
if not defined SIGNTOOL (
  for /d %%D in ("C:\Program Files (x86)\Windows Kits\10\bin\10.*") do (
    if exist "%%D\x64\signtool.exe" set "SIGNTOOL=%%D\x64\signtool.exe"
  )
)
if not defined SIGNTOOL (
  echo signtool.exe not found. Install the Windows SDK.
  exit /b 1
)

"%SIGNTOOL%" sign /f "%ODM_PFX%" /p "%ODM_PFX_PASS%" /fd SHA256 ^
  /tr http://timestamp.digicert.com /td SHA256 "%~dp0odm.exe"
if errorlevel 1 (
  echo Signing failed.
  exit /b 1
)
echo Signed: odm.exe
