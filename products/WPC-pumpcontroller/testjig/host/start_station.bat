@echo off
title WPC Production Station
cd /d "%~dp0"

echo === WPC Production Station ===
echo.
echo [1/4] Updating from GitHub ...
git pull
if errorlevel 1 echo     (git pull failed - continuing with the version already on this laptop)

echo.
echo [2/4] Making PlatformIO (pio) available ...
for /f "usebackq delims=" %%i in (`py -c "import sysconfig; print(sysconfig.get_path('scripts'))"`) do set "PYSCRIPTS=%%i"
set "PATH=%PATH%;%PYSCRIPTS%"
where pio >nul 2>&1 || echo     WARNING: pio not found - flashing will not work. Run: py -m pip install platformio

echo.
echo [3/4] Checking Python packages ...
py -m pip install -q -r requirements.txt

echo.
echo [4/4] Starting the station - the browser opens in a few seconds.
echo     Keep this window open. Press Ctrl+C here to stop.
echo.
start "" cmd /c "timeout /t 4 >nul & start http://localhost:8788/"
py wpc_station.py

echo.
echo Station stopped.
pause
