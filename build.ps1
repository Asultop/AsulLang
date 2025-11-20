$ErrorActionPreference = 'Stop'

function Find-Compiler {
    if (Get-Command g++ -ErrorAction SilentlyContinue) { return 'g++' }
    if (Get-Command clang++ -ErrorAction SilentlyContinue) { return 'clang++' }
    if (Get-Command cl -ErrorAction SilentlyContinue) { return 'cl' }
    return $null
}

$c = Find-Compiler
if (-not $c) { Write-Error "No supported compiler found (g++/clang++/cl)"; exit 1 }

Write-Host "Using compiler: $c"

if ($c -eq 'cl') {
    Write-Host "Invoking MSVC cl (non-incremental). Consider using MSBuild for complex projects."
    cl /std:c++17 /O2 Main.cpp ALangEngine.cpp /Fe:alang.exe
} else {
    # simple incremental object dir
    $objDir = Join-Path $PSScriptRoot 'build\obj'
    New-Item -ItemType Directory -Force -Path $objDir | Out-Null
    $sources = @('ALangEngine.cpp','Main.cpp')
    $jobs = [int](Get-ComputerInfo -Property 'OsNumberOfLogicalProcessors' 2>$null | ForEach-Object { $_.OsNumberOfLogicalProcessors } )
    if (-not $jobs) { $jobs = 1 }
    foreach ($src in $sources) {
        $obj = Join-Path $objDir ([IO.Path]::GetFileNameWithoutExtension($src) + '.o')
        if (!(Test-Path $obj) -or (Get-Item $src).LastWriteTime -gt (Get-Item $obj).LastWriteTime) {
            Write-Host "Compiling $src -> $obj"
            & $c -std=c++17 -O2 -fexec-charset=GBK -c $src -o $obj
        } else { Write-Host "Up-to-date: $src" }
    }
    $objs = Get-ChildItem -Path $objDir -Filter '*.o' | ForEach-Object { $_.FullName }
    & $c -std=c++17 -O2 -fexec-charset=GBK $objs -o alamg.exe
}
Write-Host "Build completed."