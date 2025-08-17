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
:: Check if task exists
:: ───────────────────────────────
schtasks /query /tn "TITAN Agent" >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] The "TITAN Agent" startup task does not exist. Nothing to uninstall.
    pause
    exit /b
)

:: ───────────────────────────────
:: Delete Startup Task
:: ───────────────────────────────
schtasks /delete /tn "TITAN Agent" /f
if %errorlevel% neq 0 (
    echo [!] Failed to remove the "TITAN Agent" task.
    pause
    exit /b
)

echo [+] "TITAN Agent" task successfully removed from startup.
pause