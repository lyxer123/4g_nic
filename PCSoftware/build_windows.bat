@echo off
setlocal
cd /d "%~dp0"

if exist ".venv\Scripts\python.exe" (
  set "PY=.venv\Scripts\python.exe"
) else (
  set "PY=python"
)

echo Using: %PY%
"%PY%" -m pip install -q -U pip
"%PY%" -m pip install -q -r requirements.txt -r requirements-build.txt
"%PY%" -m PyInstaller --clean -y 4g_nic_pc.spec
if errorlevel 1 (
  echo PyInstaller failed.
  exit /b 1
)
echo.
echo Done. Run: dist\4G_NIC_Admin.exe
endlocal
