@echo off
REM ---------------------------------------------------------------------------
REM Build helper for the DeepSeek Monitor firmware.
REM
REM Activates an ESP-IDF installation and forwards every argument to idf.py, e.g.
REM
REM   build.bat build
REM   build.bat -p COM7 -b 921600 flash monitor
REM
REM The ESP-IDF location comes from the environment when set, so nothing here is
REM tied to one machine:
REM
REM   IDF_TOOLS_PATH   directory holding the toolchains and the Python env
REM   IDF_PATH         the esp-idf checkout to activate
REM
REM The defaults match a standard Windows ESP-IDF v6.1 install.
REM ---------------------------------------------------------------------------
setlocal

if not defined IDF_TOOLS_PATH set "IDF_TOOLS_PATH=%USERPROFILE%\.espressif"
if not defined IDF_PATH       set "IDF_PATH=%USERPROFILE%\esp\esp-idf"

chcp 65001 >nul 2>&1
set PYTHONUTF8=1

if not exist "%IDF_PATH%\export.bat" (
  echo [ERROR] ESP-IDF not found at "%IDF_PATH%"
  echo         Set IDF_PATH to your esp-idf checkout and re-run.
  exit /b 1
)

call "%IDF_PATH%\export.bat" >nul 2>&1
if errorlevel 1 (
  echo [ERROR] ESP-IDF export failed. Is IDF_TOOLS_PATH correct?
  echo         IDF_TOOLS_PATH=%IDF_TOOLS_PATH%
  exit /b 1
)

cd /d "%~dp0"
idf.py %*
