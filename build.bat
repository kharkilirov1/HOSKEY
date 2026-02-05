@echo off
cd /d "%~dp0"

REM DevEco environment
set DEVECO_SDK_HOME=C:\Program Files\Huawei\DevEco Studio\sdk
set NODE_HOME=C:\Program Files\Huawei\DevEco Studio\tools\node
set HVIGOR_HOME=C:\Program Files\Huawei\DevEco Studio\tools\hvigor
set JAVA_HOME=C:\Program Files\Huawei\DevEco Studio\jbr
set PATH=%JAVA_HOME%\bin;%NODE_HOME%;%HVIGOR_HOME%\bin;%PATH%

echo Building HOSKEY (API 22)...

call "%HVIGOR_HOME%\bin\hvigorw.bat" clean 2>&1
call "%HVIGOR_HOME%\bin\hvigorw.bat" assembleHap --no-daemon 2>&1
