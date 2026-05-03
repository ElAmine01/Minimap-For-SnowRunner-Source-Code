@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
if exist "%SCRIPT_DIR%paths.local.bat" call "%SCRIPT_DIR%paths.local.bat"

:: 1. Chemins des outils et fichiers
if not defined PROJECT_PATH set "PROJECT_PATH=%SCRIPT_DIR%build\snowmap_asi.vcxproj"

if not defined MSBUILD_PATH (
    for /f "delims=" %%I in ('where MSBuild.exe 2^>nul') do (
        set "MSBUILD_PATH=%%I"
        goto :msbuild_found
    )
)
:msbuild_found

if not defined MSBUILD_PATH (
    echo [build] MSBuild.exe introuvable. Definis MSBUILD_PATH dans paths.local.bat.
    pause
    exit /b 1
)

if not exist "%PROJECT_PATH%" (
    echo [build] Projet introuvable: %PROJECT_PATH%
    pause
    exit /b 1
)

echo ===================================================
echo   CLEAN AND REBUILD PROCESS (Direct Project)
echo ===================================================

:: 2. On fait le Clean, puis le Build
:: En ciblant le .vcxproj, on n'a plus besoin de /t:Clean;Build car 
:: on va simplement dire à MSBuild de reconstruire ce projet spécifique.
:: Pour être sûr de tout nettoyer, on peut utiliser deux commandes.

echo [1/2] Cleaning old artifacts...
"%MSBUILD_PATH%" "%PROJECT_PATH%" /t:Clean /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal

echo.
echo [2/2] Rebuilding project...
"%MSBUILD_PATH%" "%PROJECT_PATH%" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal

echo.
echo ===================================================
echo   BUILD_EXIT=%ERRORLEVEL%
echo ===================================================

if %ERRORLEVEL% EQU 0 (
    echo SUCCESS: Your DLL is ready and fresh!
) else (
    echo ERROR: Build failed. Check the logs above.
)

echo.
pause