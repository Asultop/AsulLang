cmake_minimum_required(VERSION 3.15)
project(Alang LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# Try to use ccache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  message(STATUS "ccache found: ${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

set(SRCS
  ALangEngine.cpp
  Console.cpp
  Main.cpp
  src/AsulLexer.cpp
  src/AsulRuntime.cpp
  src/AsulParser.cpp
  src/AsulInterpreter.cpp
  src/AsulPackages/Std/Path/StdPath.cpp
  src/AsulPackages/Std/String/StdString.cpp
  src/AsulPackages/Std/Math/StdMath.cpp
  src/AsulPackages/Std/Time/StdTime.cpp
  src/AsulPackages/Std/Os/StdOs.cpp
  src/AsulPackages/Std/Regex/StdRegex.cpp
  src/AsulPackages/Std/Encoding/StdEncoding.cpp
  src/AsulPackages/Std/Network/StdNetwork.cpp
  src/AsulPackages/Std/Crypto/StdCrypto.cpp
  src/AsulPackages/Std/Io/StdIo.cpp
  src/AsulPackages/Std/Builtin/StdBuiltin.cpp
  src/AsulPackages/Std/Collections/StdCollections.cpp
  src/AsulPackages/Std/Array/StdArray.cpp
  src/AsulPackages/Std/Log/StdLog.cpp
  src/AsulPackages/Std/Test/StdTest.cpp
  src/AsulPackages/Std/Ffi/StdFfi.cpp
  src/AsulPackages/Std/Uuid/StdUuid.cpp
  src/AsulPackages/Std/Url/StdUrl.cpp
  src/AsulPackages/Std/Events/StdEvents.cpp
  src/AsulPackages/Json/Json.cpp
  src/AsulPackages/Xml/Xml.cpp
  src/AsulPackages/Yaml/Yaml.cpp
  src/AsulPackages/Os/Os.cpp
  src/AsulPackages/Csv/Csv.cpp
)

option(USE_READLINE "Enable readline support for REPL if available" ON)

# Readline is only available on Unix-like systems
if(USE_READLINE AND NOT WIN32)
  # 首先尝试pkg-config
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(READLINE readline)
  endif()

  if(READLINE_FOUND)
    message(STATUS "readline found via pkg-config")
    include_directories(${READLINE_INCLUDE_DIRS})
    set(READLINE_LIBS ${READLINE_LIBRARIES})
    add_compile_definitions(USE_READLINE)
  else()
    # 如果pkg-config失败，尝试直接查找
    find_path(READLINE_INCLUDE_DIRS readline/readline.h
      PATHS /usr/include /usr/local/include /opt/homebrew/opt/readline/include
    )
    find_library(READLINE_LIBRARIES readline
      PATHS /usr/lib /usr/local/lib /opt/homebrew/opt/readline/lib
    )
    
    if(READLINE_INCLUDE_DIRS AND READLINE_LIBRARIES)
      message(STATUS "readline found directly")
      include_directories(${READLINE_INCLUDE_DIRS})
      set(READLINE_LIBS ${READLINE_LIBRARIES})
      add_compile_definitions(USE_READLINE)
    else()
      message(STATUS "readline not found; REPL will fall back to getline")
    endif()
  endif()
elseif(WIN32)
  message(STATUS "Windows platform detected; REPL will use standard input (readline not supported on Windows)")
endif()

add_executable(alang ${SRCS})
target_include_directories(alang PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)

# Test Runner executable
add_executable(test_runner TestRunner.cpp)

if(READLINE_FOUND)
  target_link_libraries(alang PRIVATE ${READLINE_LIBS})
elseif(DEFINED READLINE_LIBS)
  target_link_libraries(alang PRIVATE ${READLINE_LIBS})
endif()

if(WIN32)
  # Link Winsock for Windows networking support
  target_link_libraries(alang PRIVATE ws2_32)
  target_compile_definitions(alang PRIVATE _WIN32_WINNT=0x0600)
  # Ensure UTF-8 source and execution charset for proper Unicode handling
  target_compile_options(alang PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
  # Static linking on Windows to avoid DLL dependencies (libstdc++, libgcc, libwinpthread)
  target_link_options(alang PRIVATE -static -static-libgcc -static-libstdc++)
  
  # Test runner settings for Windows
  target_compile_options(test_runner PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
  target_link_options(test_runner PRIVATE -static -static-libgcc -static-libstdc++)
else()
  # Link dl library for FFI on Unix systems
  target_link_libraries(alang PRIVATE dl)
endif()

find_package(Threads REQUIRED)

# Optional OpenSSL for std.crypto hashes
# On Windows, try common OpenSSL installation paths
if(WIN32)
  # Try Qt's OpenSSL installation
  set(OPENSSL_SEARCH_PATHS
    "C:/Qt/Tools/OpenSSLv3/Win_x64"
    "C:/OpenSSL-Win64"
    "C:/Program Files/OpenSSL-Win64"
    "C:/Program Files/OpenSSL"
    "$ENV{OPENSSL_ROOT_DIR}"
  )
  
  foreach(OPENSSL_PATH ${OPENSSL_SEARCH_PATHS})
    if(EXISTS "${OPENSSL_PATH}/include/openssl/ssl.h")
      set(OPENSSL_ROOT_DIR "${OPENSSL_PATH}")
      set(OPENSSL_INCLUDE_DIR "${OPENSSL_PATH}/include")
      # For MinGW with MSVC-built OpenSSL, we need to link against the .lib files
      # or use the DLLs directly
      if(EXISTS "${OPENSSL_PATH}/lib/libssl.lib")
        set(OPENSSL_SSL_LIBRARY "${OPENSSL_PATH}/lib/libssl.lib")
        set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_PATH}/lib/libcrypto.lib")
      elseif(EXISTS "${OPENSSL_PATH}/lib/ssl.lib")
        set(OPENSSL_SSL_LIBRARY "${OPENSSL_PATH}/lib/ssl.lib")
        set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_PATH}/lib/crypto.lib")
      endif()
      message(STATUS "Found OpenSSL at: ${OPENSSL_PATH}")
      break()
    endif()
  endforeach()
endif()

find_package(OpenSSL)
target_link_libraries(alang PRIVATE Threads::Threads)

# Link OpenSSL if available
if (OPENSSL_FOUND)
  target_link_libraries(alang PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  target_compile_definitions(alang PRIVATE ASUL_HAS_OPENSSL=1)
  
  # On Windows, copy OpenSSL DLLs to output directory
  if(WIN32 AND OPENSSL_ROOT_DIR)
    set(OPENSSL_DLL_DIR "${OPENSSL_ROOT_DIR}/bin")
    if(EXISTS "${OPENSSL_DLL_DIR}/libcrypto-3-x64.dll")
      # Copy DLLs after build
      add_custom_command(TARGET alang POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OPENSSL_DLL_DIR}/libcrypto-3-x64.dll"
        "$<TARGET_FILE_DIR:alang>/libcrypto-3-x64.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OPENSSL_DLL_DIR}/libssl-3-x64.dll"
        "$<TARGET_FILE_DIR:alang>/libssl-3-x64.dll"
        COMMENT "Copying OpenSSL DLLs..."
      )
    endif()
  endif()
else()
  message(WARNING "OpenSSL not found: std.crypto hashes will throw 'not implemented yet'.")
endif()

if(WIN32)
  # On Windows with multi-config generators (Visual Studio), executables go to build/Release/
  set_target_properties(alang PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Release
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Debug
  )
  set_target_properties(test_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Release
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Debug
  )
else()
  # On Unix, executables go to build/
  set_target_properties(alang PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
  )
  set_target_properties(test_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
  )
endif()

install(TARGETS alang RUNTIME DESTINATION bin)
