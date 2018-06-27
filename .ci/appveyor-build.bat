set "VSINSTALL=C:\Program Files (x86)\Microsoft Visual Studio %TOOLCHAIN%"
if not exist "%VSINSTALL%\" set "VSINSTALL=C:\Program Files (x86)\Microsoft Visual Studio\%TOOLCHAIN%\Community"

set "MAKE=C:\tools\make"

%MAKE% --version
perl --version

call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
where cl

%MAKE% -j2
