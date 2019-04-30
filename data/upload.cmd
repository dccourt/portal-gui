@setlocal
@echo off

rem XXX Add as command line param : destination IP.
set TARGET=http://192.168.1.67

if "%1"=="--single" goto singlefile
if "%1"=="--images" goto images
if "%1"=="--code" goto code

echo USAGE:
echo   upload --code                    : upload just code files
echo   upload --single ^<filename^>     : upload a single filename
echo   upload --images ^<subdirectory^> : upload all files in directory images\^<subdirectory^>
goto end

:code
for %%i in (index.htm *.gz) do curl -F "data=@%%i" %TARGET%/edit
goto end

:singlefile
curl -F "data=@%2" %TARGET%/edit
goto end

:images
if "%2"=="" (echo Please specify image directory to upload & goto end)
if not exist images\%2 (echo ERROR: Directory images\%2 does not exist & goto end)
for %%i in (images\%2\*.*) do (curl -F "data=@%%i;filename=images/%2/%%~nxi" %TARGET%/edit & echo Done %%i)
goto end

:end
@endlocal