@echo off
rem D_Code 配置适配入口，复用 SDK 下载及 OTA 菜单流程。
setlocal
chcp 65001 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build\package_ota.ps1"
set "OTA_EXIT=%ERRORLEVEL%"
echo.
if not "%OTA_EXIT%"=="0" echo [ERROR] OTA package generation failed. Read the message above.
pause
exit /b %OTA_EXIT%
