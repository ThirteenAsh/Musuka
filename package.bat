@echo off
setlocal
cd /d "%~dp0"

call build.bat
if errorlevel 1 exit /b 1

if exist release rmdir /s /q release
mkdir release

copy /y build\Release\musuka.exe release\musuka.exe >nul

if exist default_image (
    xcopy /e /i /y default_image release\default_image >nul
) else (
    echo default_image directory does not exist. The release will run without built-in images.
)

copy /y README.md release\README.md >nul

echo.
echo Package complete: release\

