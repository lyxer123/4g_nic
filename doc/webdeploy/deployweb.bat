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

REM ===== Flash addresses MUST match the active partition table in the repo =====
REM Current sdkconfig / sdkconfig.defaults: CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=partitions_4mb.csv
REM Excerpt (repo root partitions_4mb.csv, 4MB flash):
REM   ota_1 ends at 0x1C0000 + 0x1A0000 = 0x360000 — www starts immediately after
REM   www   littlefs  0x360000  0x10000  (64 KiB)
REM   lua   littlefs  0x370000  0x10000
REM   key_data       0x380000  0x2000
REM If you switch the project to partitions_large.csv (16MB), change the two vars below to:
REM   set "WWW_FLASH_OFFSET=0xC76000"
REM   set "WWW_SIZE_HEX=0x25800"
REM   and set WWW_SIZE_DEC to decimal of 0x25800 for mklittlefs -s (153600)
set "WWW_FLASH_OFFSET=0x360000"
set "WWW_SIZE_HEX=0x10000"
set "WWW_SIZE_DEC=65536"
REM Port: use ESPPORT env if set, else default
if not defined ESPPORT set "ESPPORT=COM8"
echo.
echo ========================================
echo Partition: www  offset !WWW_FLASH_OFFSET!  size !WWW_SIZE_HEX! (!WWW_SIZE_DEC! bytes)
echo Source table: ..\..\partitions_4mb.csv ^(verify sdkconfig if you use another CSV^)
echo ========================================
echo.
echo Note: Image must be ^<= partition size; next partition ^(lua^) starts at 0x370000 on 4MB layout.
echo After flash, serial log should show: Found partition "www": offset=0x360000
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
echo Creating LittleFS image from %STAGING_DIR% (partition size %WWW_SIZE_DEC% bytes = !WWW_SIZE_HEX!)...
echo executing: !MKFS_PATH! -c "%STAGING_DIR%" -p 256 -b 4096 -s %WWW_SIZE_DEC% -a "%OUTPUT_DIR%\www.bin"
!MKFS_PATH! -c "%STAGING_DIR%" -p 256 -b 4096 -s %WWW_SIZE_DEC% -a "%OUTPUT_DIR%\www.bin"

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
echo   Expected www partition offset: !WWW_FLASH_OFFSET!
echo   If this doesn't match the compiled partition table, flashing will land in the wrong place.
echo.
echo Checking image file size...
for %%A in ("%OUTPUT_DIR%\www.bin") do (
    set "IMAGE_SIZE=%%~zA"
    echo Image file size: !IMAGE_SIZE! bytes
)
if !IMAGE_SIZE! gtr %WWW_SIZE_DEC% (
    echo.
    echo ERROR: www.bin (!IMAGE_SIZE! bytes^) is larger than www partition (!WWW_SIZE_DEC! bytes^).
    echo Reduce web assets or enlarge www in the partition table ^+ mklittlefs -s / flash offset.
    pause
    exit /b 1
)
echo.
echo Executing: python -m esptool --port %ESPPORT% write_flash !WWW_FLASH_OFFSET! "%OUTPUT_DIR%\www.bin"
python -m esptool --port %ESPPORT% write_flash !WWW_FLASH_OFFSET! "%OUTPUT_DIR%\www.bin"

if errorlevel 1 (
    echo.
    echo ERROR: Failed to flash image
    echo Please check:
    echo   1. Is ESP32 device connected on %ESPPORT%?
    echo   2. Is device in flash mode (hold BOOT button while pressing RESET)?
    echo   3. Is partition offset correct? Expected www @ !WWW_FLASH_OFFSET! — see partitions_4mb.csv
    echo   4. Did you rebuild / full-flash after changing partition table?
    echo   5. build\partition_table\partition-table.bin is generated from root partitions_4mb.csv
    pause
    exit /b 1
) else (
    echo.
    echo ========================================
    echo Successfully flashed LittleFS image to device!
    echo ========================================
    echo.
    echo Flash details:
    echo   Offset used: !WWW_FLASH_OFFSET!
    echo   Image file: %OUTPUT_DIR%\www.bin
    echo.
    echo Next steps:
    echo   1. Restart the ESP32 device
    echo   2. Check serial monitor for filesystem initialization logs
    echo   3. Look for: "Found partition \"www\": offset=0x..."
    echo   4. VERIFY the offset matches: !WWW_FLASH_OFFSET!
    echo   5. If offset mismatch, files were flashed to wrong location!
    echo.
    echo Expected log output:
    echo   I ... fs_manager: Found partition "www": offset=0x360000 (4MB layout)
    echo   I ... fs_manager: Found required file: /www/index.html
    echo   I ... fs_manager: Found required file: /www/js/app.js
    echo   I ... fs_manager: Found required file: /www/css/style.css
    echo.
)

echo.
echo Frontend deployment completed!
echo Note: Web files will be available at http://^<device_ip^>/ after reboot
pause
