@echo off
setlocal
cd /d "%~dp0"

echo Building TouchscreenConnect (x64 Release)...
if not exist "bin\x64" mkdir "bin\x64"

msbuild TouchscreenConnect.vcxproj /p:Configuration=Release /p:Platform=x64 /nologo /v:m
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

if exist "bin\x64\Release\TouchscreenConnect.exe" (
    copy /y "bin\x64\Release\TouchscreenConnect.exe" "bin\x64\TouchscreenConnect.exe" >nul
    copy /y "bin\x64\Release\TouchscreenConnect.exe" "TouchscreenConnect.exe" >nul
)

echo Build succeeded: TouchscreenConnect.exe
exit /b 0
