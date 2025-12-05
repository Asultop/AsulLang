# Windows Build Guide for ALang

This guide provides detailed instructions for building ALang on Windows platforms.

## Prerequisites

### Required Software

1. **Visual Studio 2017 or later** (Visual Studio 2022 recommended)
   - Install "Desktop development with C++" workload
   - Ensure C++17 support is enabled
   
2. **CMake 3.15 or later**
   - Download from https://cmake.org/download/
   - Add to PATH during installation

3. **Git for Windows**
   - Download from https://git-scm.com/download/win
   
### Optional Dependencies

4. **OpenSSL** (for cryptography features)
   ```powershell
   # Using Chocolatey
   choco install openssl -y
   
   # Or download pre-built binaries from:
   # https://slproweb.com/products/Win32OpenSSL.html
   ```

## Build Instructions

### 1. Clone the Repository

```powershell
git clone https://github.com/Asultop/AsulLang.git
cd AsulLang
git submodule update --init --recursive
```

### 2. Configure with CMake

#### Option A: Using Visual Studio Generator (Recommended)

```powershell
mkdir build
cd build

# For x64 build
cmake .. -G "Visual Studio 17 2022" -A x64

# If OpenSSL is installed in non-standard location:
cmake .. -G "Visual Studio 17 2022" -A x64 `
  -DOPENSSL_ROOT_DIR="C:/Program Files/OpenSSL-Win64" `
  -DOPENSSL_INCLUDE_DIR="C:/Program Files/OpenSSL-Win64/include"
```

#### Option B: Using Ninja (Faster builds)

```powershell
# First, open "x64 Native Tools Command Prompt for VS 2022"
mkdir build
cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
```

### 3. Build the Project

#### With Visual Studio:

```powershell
cmake --build . --config Release
```

Or open `ALang.sln` in Visual Studio and build from IDE.

#### With Ninja:

```powershell
ninja
```

### 4. Run Tests

```powershell
cd Release  # or Debug
./alang.exe ../Example/array_select_methods.alang
```

## Platform-Specific Notes

### Windows-Specific Features

ALang on Windows includes:

- **Winsock2 networking**: Full socket API support via `std.network`
- **FFI with .dll loading**: Load and call functions from Windows DLLs
- **Cross-platform file paths**: Automatic handling of Windows path separators
- **Process management**: Compatible `std.os` functions using Windows APIs

### Header File Compatibility

The following headers are automatically handled for Windows:

```cpp
// Network (StdNetwork.cpp)
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#endif

// FFI (StdFfi.cpp)
#ifdef _WIN32
  #include <windows.h>
#endif

// OS (StdOs.cpp)
#ifdef _WIN32
  #include <windows.h>
  #include <process.h>
#endif
```

### Compiler Compatibility

| Compiler | Version | Status |
|----------|---------|--------|
| MSVC 2017 | 19.10+ | ✅ Supported |
| MSVC 2019 | 19.20+ | ✅ Supported |
| MSVC 2022 | 19.30+ | ✅ Recommended |
| MinGW-w64 | GCC 8.0+ | ⚠️ Experimental |
| Clang-cl | 10.0+ | ⚠️ Experimental |

### Known Issues

1. **Readline Support**: Windows doesn't have native readline. The REPL falls back to `std::getline`.
   - Workaround: Use Git Bash or WSL for better REPL experience

2. **OpenSSL Discovery**: CMake may not auto-detect OpenSSL on Windows
   - Solution: Explicitly set OpenSSL paths as shown above

3. **Path Separators**: ALang handles both `/` and `\` in paths automatically

## Troubleshooting

### Build Errors

**Error: "Cannot open include file: 'winsock2.h'"**
```
Solution: Ensure Windows SDK is installed with Visual Studio
```

**Error: "unresolved external symbol WSAStartup"**
```
Solution: ws2_32.lib should be linked automatically. Check CMakeLists.txt
```

**Error: "OpenSSL not found"**
```
Solution: Install OpenSSL or build without crypto features
The project will compile with a warning about missing crypto functions
```

### Runtime Errors

**Error: "The code execution cannot proceed because VCRUNTIME140.dll was not found"**
```
Solution: Install Visual C++ Redistributable 2015-2022:
https://aka.ms/vs/17/release/vc_redist.x64.exe
```

## Cross-Compilation

To cross-compile ALang for Windows from Linux:

```bash
# Install MinGW cross-compiler
sudo apt-get install mingw-w64

# Configure with MinGW toolchain
mkdir build-win
cd build-win
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw-w64.cmake
make
```

## CI/CD Integration

For GitHub Actions, see `.github/workflows/windows-build.yml`:

```yaml
- name: Build on Windows
  run: |
    mkdir build
    cd build
    cmake .. -G "Visual Studio 17 2022" -A x64
    cmake --build . --config Release
```

## Additional Resources

- [CMake Documentation](https://cmake.org/documentation/)
- [Visual Studio C++ Documentation](https://docs.microsoft.com/en-us/cpp/)
- [ALang GitHub Repository](https://github.com/Asultop/AsulLang)

## Support

For Windows-specific build issues, please open an issue on GitHub with:
- Your Windows version
- Visual Studio/compiler version
- Full error output
- CMake configuration output
