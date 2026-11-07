@echo off
rem WSPR Scheduler Map 3: first push to GitHub (run once; afterwards use gitpush.bat)
cd /d C:\WSPR-Map-3
if errorlevel 1 (echo Folder C:\WSPR-Map-3 not found & pause & exit /b 1)
if not exist .git git init -b main
git add .
git commit -m "WSPR Scheduler Map 3 - Qt6 port, V0.3.0"
git branch -M main
git remote remove origin 2>nul
git remote add origin https://github.com/npalmer1/wspr-scheduler-map-3.git
echo.
git remote -v
echo.
git push -u origin main
pause
