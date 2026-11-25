@echo off
REM ============================================================
REM  Campus Radar - one-click launcher
REM  Put this file in the project root (the "DSA PBL" folder,
REM  next to the backend, engine, frontend and db folders).
REM ============================================================
setlocal
cd /d "%~dp0"

echo [1/3] Building backend...
cd backend
where gcc >nul 2>nul
if errorlevel 1 (
    echo gcc not found. Using existing campus_backend.exe if present.
) else (
    gcc -O2 -Wall -Wextra -std=c11 -I../engine server.c ../engine/campus.c ../engine/heap.c ../engine/campus_router.c ../engine/segment_tree.c ../engine/map_loader.c ../engine/timetable.c ../engine/engine_api.c -o campus_backend.exe -lm -lws2_32
    if errorlevel 1 (
        echo Build failed. Fix the errors above and run again.
        pause
        exit /b 1
    )
)

if not exist campus_backend.exe (
    echo campus_backend.exe not found and could not be built.
    pause
    exit /b 1
)

echo [2/3] Starting server on http://localhost:8000 ...
REM Server must start inside the backend folder because it reads ../db/campus_map.json
start "Campus Radar Backend" cmd /k campus_backend.exe 8000

echo [3/3] Opening the web app...
timeout /t 2 /nobreak >nul
cd ..
start "" "frontend\index.html"

echo.
echo Done. Close the "Campus Radar Backend" window to stop the server.
endlocal