@echo off
rem WSPR Scheduler Map 3: commit everything and push it to GitHub
cd /d D:\WSPR-Map-3
set "msg="
set /p msg=Commit message (Enter for "Update"): 
if "%msg%"=="" set "msg=Update"
git add .
git commit -m "%msg%"
git push
pause
