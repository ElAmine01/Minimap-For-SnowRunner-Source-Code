@echo off

:: 1. Chemins des outils et fichiers
set MSBUILD_PATH="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
:: On cible directement le projet .vcxproj pour éviter l'erreur MSB1008
set PROJECT_PATH="C:\Users\jeuxv\Documents\snowmap\build\snowmap_asi.vcxproj"

echo ===================================================
echo   CLEAN AND REBUILD PROCESS (Direct Project)
echo ===================================================

:: 2. On fait le Clean, puis le Build
:: En ciblant le .vcxproj, on n'a plus besoin de /t:Clean;Build car 
:: on va simplement dire à MSBuild de reconstruire ce projet spécifique.
:: Pour être sûr de tout nettoyer, on peut utiliser deux commandes.

echo [1/2] Cleaning old artifacts...
%MSBUILD_PATH% %PROJECT_PATH% /t:Clean /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal

echo.
echo [2/2] Rebuilding project...
%MSBUILD_PATH% %PROJECT_PATH% /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal

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