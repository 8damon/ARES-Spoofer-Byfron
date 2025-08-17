@echo off
:: ───────────────────────────────
:: UAC Elevation Check
:: ───────────────────────────────
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Administrator elevation required. Prompting...
    set "params=%*"
    echo Set UAC = CreateObject^("Shell.Application"^) > "%temp%\uac.vbs"
    echo UAC.ShellExecute "cmd.exe", "/c ""%~s0"" %params%", "", "runas", 1 >> "%temp%\uac.vbs"
    "%temp%\uac.vbs"
    del "%temp%\uac.vbs"
    exit /b
)

:: ───────────────────────────────
:: Check if task already exists
:: ───────────────────────────────
schtasks /query /tn "TITAN Agent" >nul 2>&1
if %errorlevel% equ 0 (
    echo [!] The "TITAN Agent" startup task already exists. Aborting install.
    pause
    exit /b
)

:: ───────────────────────────────
:: Create Startup Task
:: ───────────────────────────────
set "exe_path=%~dp0TITAN Spoofer.exe"

schtasks /create /tn "TITAN Agent" /tr "\"%exe_path%\"" /sc onlogon /rl highest /f
if %errorlevel% neq 0 (
    echo [!] Failed to create startup task.
    pause
    exit /b
)

echo [+] "TITAN Agent" installed to startup successfully.
pause
