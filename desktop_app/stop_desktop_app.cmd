@echo off
setlocal
set "SCRIPT_DIR=%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%stop_desktop_app.ps1"
set "EXIT_CODE=%ERRORLEVEL%"

if not "%EXIT_CODE%"=="0" (
  echo.
  echo Unable to stop Stingray Inventory Desktop. Exit code: %EXIT_CODE%
  echo Press any key to close.
  pause >nul
)

endlocal
exit /b %EXIT_CODE%
