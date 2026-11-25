@echo off
setlocal
set LOBBY_ID=%~1
if "%LOBBY_ID%"=="" (
  set /p LOBBY_ID="Enter Steam Lobby ID (or press Enter to auto-join friend / accept Steam invite): "
)
if "%LOBBY_ID%"=="" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0play_coop_join.ps1" -Steam
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0play_coop_join.ps1" -Steam -LobbyId "%LOBBY_ID%"
)
if errorlevel 1 pause
