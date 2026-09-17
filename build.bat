@echo off
setlocal enabledelayedexpansion

set "PATH=%~dp0third_party\w64devkit\bin;%PATH%"

echo ===================================================
echo   Compilando Zelda 3 - In-Game Overlay Edition
echo ===================================================

if not exist "third_party\w64devkit\bin\make.exe" (
  echo ERRO: Compilador make/gcc nao encontrado em third_party\w64devkit\bin!
  exit /b 1
)

if not exist "zelda3_assets.dat" (
  echo AVISO: zelda3_assets.dat nao encontrado! Extraindo assets da ROM...
  python assets/restool.py --extract-from-rom
)

make -j%NUMBER_OF_PROCESSORS%
if errorlevel 1 (
  echo ERRO durante a compilacao!
  exit /b 1
)

if exist "third_party\SDL2-2.26.3\lib\x64\SDL2.dll" (
  copy /Y "third_party\SDL2-2.26.3\lib\x64\SDL2.dll" . >nul
)

echo ===================================================
echo   Compilacao concluida com sucesso!
echo   Executavel gerado: zelda3.exe
echo ===================================================
