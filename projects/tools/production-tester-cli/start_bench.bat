@echo off
setlocal
cd /d "%~dp0"

echo ============================================
echo  FG1 Production Tester -- starting bench
echo ============================================

echo.
echo Pulling latest code...
git pull
if errorlevel 1 (
    echo.
    echo WARNING: git pull failed -- continuing with whatever code is
    echo already on this machine. Check your network/WiFi if this keeps
    echo happening; the tool will still work, just possibly out of date.
)

echo.
echo Checking dependencies...
py -m pip install -q -r requirements.txt
if errorlevel 1 (
    echo.
    echo WARNING: installing dependencies had a problem -- the server
    echo may fail to start. See the error above.
)

echo.
echo Starting the server...
start "FG1 Production Tester -- KEEP THIS WINDOW OPEN" py web_ui.py

echo Waiting for it to come up...
timeout /t 3 /nobreak >nul

echo Opening the browser...
start http://localhost:8420

echo.
echo ============================================
echo  Done. The server is running in the OTHER
echo  window titled "FG1 Production Tester --
echo  KEEP THIS WINDOW OPEN". Leave that window
echo  open while you're testing; close it when
echo  you're done for the day.
echo.
echo  This window (the launcher) is safe to close
echo  now.
echo ============================================
echo.
pause
