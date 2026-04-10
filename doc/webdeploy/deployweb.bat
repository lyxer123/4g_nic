@echo off
setlocal enabledelayedexpansion

echo ESP-IDF Web Frontend Deployment Batch Script
echo ===========================================

set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
REM Check if mklittlefs.exe tool exists (script dir or current dir)
if exist "%SCRIPT_DIR%\mklittlefs.exe" (
    set "MKFS_PATH=%SCRIPT_DIR%\mklittlefs.exe"
) else if exist "mklittlefs.exe" (
    set "MKFS_PATH=mklittlefs.exe"
) else (
    echo Error: mklittlefs.exe not found in %SCRIPT_DIR% or current directory
    echo Download from https://github.com/earlephilhower/mklittlefs/releases
    pause
    exit /b 1
)
echo Using mklittlefs: !MKFS_PATH!

REM Check if esptool is available
echo Checking if esptool is available...
python -c "import esptool; print('esptool version:')" >nul 2>&1
if errorlevel 1 (
    echo Error: esptool is not available
    pause
    exit /b 1
) else (
    echo esptool is available
)

REM Flash offsets MUST match project root partitions_4mb.csv (www partition only).
REM   www: offset 0x360000, size 0x10000 (64KB) — 4MB flash layout
set "OFFSET=0x360000"
set "WWW_SIZE=65536"
REM Port: use ESPPORT env if set, else default COM5
if not defined ESPPORT set "ESPPORT=COM8"
echo.
echo ========================================
echo Using www partition offset: !OFFSET! size 0x10000 (64KB) - see partitions_4mb.csv
echo ========================================
echo.
echo Note: Offset must match partition table. After flash, verify in serial: "Found partition \"www\": offset=0x360000"
echo.

REM Source = repo webPage/ folder; output = this script's directory
set "SOURCE_DIR=%SCRIPT_DIR%\..\..\webPage"
set "OUTPUT_DIR=%SCRIPT_DIR%"
set "STAGING_DIR=%OUTPUT_DIR%\www_staging"
echo Source (Web files): %SOURCE_DIR%
echo Output / staging:  %OUTPUT_DIR%
echo.

REM Copy data to staging then gzip each file (device serves .gz with Content-Encoding: gzip to save Flash and PSRAM)
echo Preparing staging directory...
if exist "%STAGING_DIR%" rmdir /s /q "%STAGING_DIR%"
mkdir "%STAGING_DIR%"
xcopy /E /I /Y "%SOURCE_DIR%\*" "%STAGING_DIR%\" >nul
echo Compressing files with gzip (per-file)...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -Path '%STAGING_DIR%' -Recurse -File | Where-Object { $_.Extension -ne '.gz' } | ForEach-Object { $f=$_.FullName; $g=$f+'.gz'; $in=[System.IO.File]::OpenRead($f); $out=[System.IO.File]::Create($g); $gz=New-Object System.IO.Compression.GZipStream($out,[System.IO.Compression.CompressionMode]::Compress); $in.CopyTo($gz); $gz.Close(); $out.Close(); $in.Close(); [System.IO.File]::Delete($f) }"
if errorlevel 1 (
    echo Warning: PowerShell gzip failed, creating image from uncompressed staging.
)

REM Execute mklittlefs command to create filesystem image from staging (gzipped files)
echo Creating LittleFS image from %STAGING_DIR% (size %WWW_SIZE% = 0x10000 = 64KB)...
echo executing: !MKFS_PATH! -c "%STAGING_DIR%" -p 256 -b 4096 -s %WWW_SIZE% -a "%OUTPUT_DIR%\www.bin"
!MKFS_PATH! -c "%STAGING_DIR%" -p 256 -b 4096 -s %WWW_SIZE% -a "%OUTPUT_DIR%\www.bin"

if errorlevel 1 (
    echo Error: Failed to create LittleFS image
    pause
    exit /b 1
) else (
    echo Successfully created LittleFS image: %OUTPUT_DIR%\www.bin
)

echo.
echo Flashing image to device...
echo.
echo IMPORTANT: Verifying partition offset...
echo   Expected www partition offset: !OFFSET!
echo   If this doesn't match the compiled partition table, flashing will fail!
echo.
echo Checking image file size...
for %%A in ("%OUTPUT_DIR%/www.bin") do (
    set "IMAGE_SIZE=%%~zA"
    echo Image file size: !IMAGE_SIZE! bytes
)
echo.
echo Executing: python -m esptool --port %ESPPORT% write_flash !OFFSET! "%OUTPUT_DIR%\www.bin"
python -m esptool --port %ESPPORT% write_flash !OFFSET! "%OUTPUT_DIR%\www.bin"

if errorlevel 1 (
    echo.
    echo ERROR: Failed to flash image
    echo Please check:
    echo   1. Is ESP32 device connected on %ESPPORT%?
    echo   2. Is device in flash mode (hold BOOT button while pressing RESET)?
    echo   3. Is partition offset address correct? Expected: !OFFSET!
    echo   4. Did you rebuild the project after changing partition table?
    echo   5. Check build\partition_table\partition-table.csv or partitions_4mb.csv for actual offset
    pause
    exit /b 1
) else (
    echo.
    echo ========================================
    echo Successfully flashed LittleFS image to device!
    echo ========================================
    echo.
    echo Flash details:
    echo   Offset used: !OFFSET!
    echo   Image file: %OUTPUT_DIR%/www.bin
    echo.
    echo Next steps:
    echo   1. Restart the ESP32 device
    echo   2. Check serial monitor for filesystem initialization logs
    echo   3. Look for: "Found partition \"www\": offset=0x..."
    echo   4. VERIFY the offset matches: !OFFSET!
    echo   5. If offset mismatch, files were flashed to wrong location!
    echo.
    echo Expected log output:
    echo   I ... fs_manager: Found partition "www": offset=0x... (should match !OFFSET!)
    echo   I ... fs_manager: Found required file: /www/index.html
    echo   I ... fs_manager: Found required file: /www/js/app.js
    echo   I ... fs_manager: Found required file: /www/css/style.css
    echo.
)

echo.
echo Frontend deployment completed!
echo Note: Web files will be available at http://<device_ip>/ after reboot
pause