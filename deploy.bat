@echo off
REM Deploy SnowMap.asi and dinput8.dll into the user's SnowRunner install.

setlocal EnableDelayedExpansion
set "SCRIPT_DIR=%~dp0"
if exist "%SCRIPT_DIR%paths.local.bat" call "%SCRIPT_DIR%paths.local.bat"

if not defined GAME_BIN (
    if defined ProgramFiles(x86) (
        set "GAME_BIN=%ProgramFiles(x86)%\Steam\steamapps\common\SnowRunner\Sources\Bin"
    ) else (
        set "GAME_BIN=%ProgramFiles%\Steam\steamapps\common\SnowRunner\Sources\Bin"
    )
)
if not defined BUILD_DIST set "BUILD_DIST=%SCRIPT_DIR%build\dist"

if not exist "!GAME_BIN!\SnowRunner.exe" (
    echo [deploy] SnowRunner.exe introuvable dans : !GAME_BIN!
    echo [deploy] Verifie le chemin GAME_BIN dans deploy.bat.
    pause
    exit /b 1
)

if not exist "%BUILD_DIST%\SnowMap.asi" (
    echo [deploy] SnowMap.asi n'a pas encore ete compile dans build/dist/ !
    pause
    exit /b 1
)

if not exist "%BUILD_DIST%\dinput8.dll" (
    echo [deploy] dinput8.dll n'a pas encore ete compile dans build/dist/ !
    pause
    exit /b 1
)

echo[deploy] Copie de SnowMap.asi et dinput8.dll...
echo ---------------------------------------------------

REM Copie du mod (.asi)
copy /Y "%BUILD_DIST%\SnowMap.asi" "%GAME_BIN%\SnowMap.asi"
if errorlevel 1 goto :copy_fail

REM Copie du proxy (.dll)
copy /Y "%BUILD_DIST%\dinput8.dll" "%GAME_BIN%\dinput8.dll"
if errorlevel 1 goto :copy_fail

REM Le dossier SnowMap/ (options.json) n'est pas copie pour ne pas ecraser la config.

echo ---------------------------------------------------
echo [deploy] Succes ! Le mod et son proxy sont installes.
echo [deploy] Logs: %GAME_BIN%\SnowMap\SnowMap.log
pause
exit /b 0

:copy_fail
echo.
echo [deploy] ERREUR : La copie a echoue.
echo Verifie que le jeu SnowRunner est bien FERME et relance en Administrateur si besoin.
pause
exit /b 1