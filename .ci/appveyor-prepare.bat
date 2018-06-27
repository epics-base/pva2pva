set "VSINSTALL=C:\Program Files (x86)\Microsoft Visual Studio %TOOLCHAIN%"
if not exist "%VSINSTALL%\" set "VSINSTALL=C:\Program Files (x86)\Microsoft Visual Studio\%TOOLCHAIN%\Community"

set "MAKE=C:\tools\make"

echo [INFO] Installing Make 4.1
curl -fsS --retry 3 -o C:\tools\make-4.1.zip https://epics.anl.gov/download/tools/make-4.1-win64.zip
cd \tools
"C:\Program Files\7-Zip\7z" e make-4.1.zip

%MAKE% --version
perl --version

echo [INFO] APPVEYOR_BUILD_WORKER_IMAGE=%APPVEYOR_BUILD_WORKER_IMAGE%

set EPICS_HOST_ARCH=windows-x64
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
where cl

echo EPICS_BASE=%CD%\deps\epics-base> RELEASE.local
echo PVDATA=%CD%\deps\pvDataCPP>> RELEASE.local
echo PVACCESS=%CD%\deps\pvAccessCPP>> RELEASE.local

mkdir deps
cp RELEASE.local deps/RELEASE.local
cd deps

git clone --branch 3.16 https://github.com/epics-base/epics-base.git
git clone https://github.com/epics-base/pvDataCPP.git
git clone https://github.com/epics-base/pvAccessCPP.git

%MAKE% -j2 -C epics-base
%MAKE% -j2 -C pvDataCPP
%MAKE% -j2 -C pvAccessCPP

cd ..
