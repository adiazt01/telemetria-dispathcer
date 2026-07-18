@echo off
REM ============================================================================
REM Script de compilacion para Windows (usando Visual Studio Developer Prompt)
REM ============================================================================

echo ========================================
echo   Build Script - Grupo 8
echo   Sistema de Telemetria y Control
echo ========================================
echo.

REM Crear directorio de salida si no existe
if not exist build mkdir build

REM Compilar cada modulo
echo Compilando modulos...
echo.

echo [1/4] Sensor (Carlos Brito)...
cl /W4 /EHsc /O2 /D_WIN32_WINNT=0x0501 sensor.c common.c /Fe:build\sensor.exe
if errorlevel 1 goto error

echo.
echo [2/4] Broker (Jesus Guzman)...
cl /W4 /EHsc /O2 /D_WIN32_WINNT=0x0501 broker.c common.c /Fe:build\broker.exe
if errorlevel 1 goto error

echo.
echo [3/4] Dispatcher (Armando Diaz)...
cl /W4 /EHsc /O2 /D_WIN32_WINNT=0x0501 dispatcher.c common.c /Fe:build\dispatcher.exe
if errorlevel 1 goto error

echo.
echo [4/4] Monitor (Jose Marquez)...
cl /W4 /EHsc /O2 /D_WIN32_WINNT=0x0501 monitor.c common.c /Fe:build\monitor.exe
if errorlevel 1 goto error

echo.
echo ========================================
echo   Compilacion exitosa!
echo ========================================
echo.
echo Ejecutables en: build\
echo.
echo Orden de ejecucion:
echo   1. broker.exe
echo   2. dispatcher.exe
echo   3. monitor.exe
echo   4. sensor.exe (N instancias)
echo.
goto fin

:error
echo.
echo ========================================
echo   ERROR durante la compilacion!
echo ========================================
exit /b 1

:fin
pause
