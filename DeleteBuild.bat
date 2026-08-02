@echo off
rem ============================================================================
rem Script Name:   Clean_Project_Builds_And_Desktop_Ini.bat
rem Description:   Recursively deletes all "build" folders and "desktop.ini" 
rem                files starting from the directory where the script is located.
rem ============================================================================

echo Deleting all build folders and desktop.ini files...

REM Navigate to script directory (project root)
cd /d %~dp0
rem ^ Changes directory to the folder where this batch script resides

REM Find and delete all "build" folders recursively
for /d /r %%d in (build) do (
    if exist "%%d" (
        echo Deleting folder: %%d
        rmdir /s /q "%%d"
    )
)
rem ^ Loops through directories, checks if "build" exists, and forcefully removes it

REM Find and delete all "desktop.ini" files recursively
for /r %%f in (desktop.ini) do (
    if exist "%%f" (
        echo Deleting file: %%f
        attrib -h -s "%%f"
        del /f /q "%%f"
    )
)
rem ^ Loops through files, strips hidden/system attributes from "desktop.ini", and deletes it

echo Done!
rem ^ Prints completion message to the console

REM pause
rem ^ Pauses execution and prompts the user to press any key to exit