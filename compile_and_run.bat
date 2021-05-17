call "P:\Programme\Visual Studio Community 2019\VC\Auxiliary\Build\vcvars64.bat"
cd backend
cl /EHsc /Zi main.cpp compiler\hardcoded_functions.cpp
EXIT /B %ERRORLEVEL%
