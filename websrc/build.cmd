@ECHO OFF
@SETLOCAL

if "%1"=="--clean" goto clean

echo Extracting jQueryUI files...
set JQUI=jquery-ui-1.12.1.custom
rem Use the minimised versions of these files
set MIN=.min
copy /y %JQUI%\jquery-ui%MIN%.css           .\jquery-ui.css
copy /y %JQUI%\jquery-ui.structure%MIN%.css .\jquery-ui.structure.css
copy /y %JQUI%\jquery-ui.theme%MIN%.css     .\jquery-ui.theme.css
copy /y %JQUI%\jquery-ui%MIN%.js            .\jquery-ui.js

echo Processing JS...
REM /b avoids appending an EOF character
copy iziModal.min.js+jquery-ui.js+jquery.ui.touch-punch.min.js tail.js /b
copy jquery-3.4.0.min.js head.js /b

call :compressCopy tail.js
call :compressCopy head.js

echo Processing CSS...
copy iziModal.min.css+jquery-ui.css+jquery-ui.structure.css+jquery-ui.theme.css combined.css /b

call :compressCopy combined.css

REM Save the output suitable for ESP SPIFFS
copy /y head.js.gz built\
copy /y tail.js.gz built\
copy /y combined.css.gz built\

REM Save the output suitable for web testing
copy /y head.js ..\data\static\
copy /y tail.js ..\data\static\
copy /y combined.css ..\data\static\

goto end

:clean
del jquery-ui*.css
del jquery-ui*.js
del head.js
del tail.js
del combined.css
del *.gz
del /q built\*.*

goto end

:compressCopy
SETLOCAL
if exist %1.gz del /q %1.gz
copy /y %1 %1.copy
gzip %1.copy
ren %1.copy.gz %1.gz
ENDLOCAL
goto :eof

:end
ENDLOCAL