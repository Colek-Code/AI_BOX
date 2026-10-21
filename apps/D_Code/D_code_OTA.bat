@echo off
rem D_Code configuration adapter; reuses the SDK download and OTA menu flow.
setlocal
chcp 65001 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build\package_ota.ps1"
set "OTA_EXIT=%ERRORLEVEL%"
echo.
if not "%OTA_EXIT%"=="0" echo [错误] OTA 打包失败，请查看上方错误信息。
pause
exit /b %OTA_EXIT%
