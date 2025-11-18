$ErrorActionPreference = 'Continue'; 
if (Get-Command g++ -ErrorAction SilentlyContinue) { 
    Write-Host "Using g++ to build...";
    g++ -std=c++17 -O2 Main.cpp ALangEngine.cpp -o alang.exe;
} elseif (Get-Command cl -ErrorAction SilentlyContinue) { 
    Write-Host "Using cl to build...";
    cl /std:c++17 /O2 Main.cpp ALangEngine.cpp /Fe:alang.exe; 
} else { 
    Write-Error "No compiler found (g++/cl)"; 
    exit 1; 
} 
Write-Host "Build completed.";