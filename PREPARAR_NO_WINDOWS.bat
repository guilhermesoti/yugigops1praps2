@echo off
setlocal
set "DISC=%~1"
if not defined DISC (
  echo Arraste o arquivo BIN ou CUE sobre este BAT.
  echo Tambem pode informar o caminho abaixo.
  set /p "DISC=Disco: "
)
if not defined DISC exit /b 1
set "DISC=%DISC:"=%"
where py >nul 2>nul
if errorlevel 1 goto usepython
py -3 "%~dp0tools\preparar.py" "%DISC%" --modelo "%~dp0YFM.ELF" -o "%~dp0YFM_DISCO.ELF"
goto done
:usepython
python "%~dp0tools\preparar.py" "%DISC%" --modelo "%~dp0YFM.ELF" -o "%~dp0YFM_DISCO.ELF"
:done
set "RESULT=%ERRORLEVEL%"
pause
exit /b %RESULT%
