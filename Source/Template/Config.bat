@echo off

rem | General
set "PROGRAM_NAME=$projectname$"
set "PROGRAM_VERSION=1.0.0"
set "PROGRAM_IDENTIFIER=com.example.$safeprojectname$"
set "CACHE_ENVIRONMENT=TRUE"

rem | Release
set "ENABLE_ENCRYPTION=TRUE"

rem | Each platform's own settings are in its folder in Framework\Automations\Platforms, in Config.bat
