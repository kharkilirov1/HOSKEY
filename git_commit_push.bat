@echo off
echo Starting Git commit and push process...

REM Navigate to the project directory
cd /d "C:\Users\Kharki\Desktop\HOSKEY"

echo Adding all files to Git...
git add .

echo Checking Git status...
git status

echo.
set /p commit_msg="Enter commit message (or press Enter for default): "

if "%commit_msg%"=="" (
    set commit_msg=NAPI conversion: OpenBoard C++ to HarmonyOS NAPI
)

echo.
echo Committing with message: %commit_msg%
git commit -m "%commit_msg%"

echo.
echo Pushing to remote repository...
git push

echo.
echo Git commit and push process completed!
pause