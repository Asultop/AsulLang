@echo off
echo Building ALang Engine (smart)...

where g++ >nul 2>&1
if %ERRORLEVEL%==0 (
	set CXX=g++
) else (
	echo g++ not found, attempting cl...
	where cl >nul 2>&1
	if %ERRORLEVEL%==0 (
		echo Please use PowerShell build script for MSVC (build.ps1)
		exit /b 1
	) else (
		echo No supported compiler found (g++/cl).
		exit /b 1
	)
)

REM Simple non-incremental build on Windows (use PowerShell for advanced flow)
%CXX% -std=c++17 -O2 ALangEngine.cpp Main.cpp -o alang.exe -fexec-charset=GBK
if %ERRORLEVEL%==0 (
	echo Build completed.
) else (
	echo Build failed with exit code %ERRORLEVEL%.
	exit /b %ERRORLEVEL%
)