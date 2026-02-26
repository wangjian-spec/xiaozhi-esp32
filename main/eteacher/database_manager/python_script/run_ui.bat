@echo off
setlocal
set "PYTHON_EXE=C:\Users\wj\AppData\Local\Programs\Python\Python313\python.exe"
set "SCRIPT=%~dp0json_to_question_db_ui.py"

if not exist "%PYTHON_EXE%" (
  echo [ERROR] Python not found: %PYTHON_EXE%
  echo Please install Python 3.13 or update PYTHON_EXE in run_ui.bat
  pause
  exit /b 1
)

"%PYTHON_EXE%" "%SCRIPT%"
if errorlevel 1 (
  echo.
  echo [ERROR] Launch failed with exit code %errorlevel%
  pause
)
endlocal
