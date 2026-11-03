@echo off
setlocal
py -3 -c "import sys; sys.exit(sys.version_info < (3,9))" >nul 2>&1
if not errorlevel 1 goto pythonlauncher
python -c "import sys; sys.exit(sys.version_info < (3,9))" >nul 2>&1
if not errorlevel 1 goto pythoncommand
echo Python 3.9 or newer is required. See RELEASE.md.
set "flash_result=2"
goto finished
:pythonlauncher
py -3 "%~dp0flash.py" %*
set "flash_result=%errorlevel%"
goto finished
:pythoncommand
python "%~dp0flash.py" %*
set "flash_result=%errorlevel%"
:finished
if not "%flash_result%"=="0" echo The operation did not complete. Read the message above.
if "%~1"=="" pause
exit /b %flash_result%
