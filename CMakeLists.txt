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
  src/AsulPackages/Json/Json.cpp
  src/AsulPackages/Xml/Xml.cpp
  src/AsulPackages/Yaml/Yaml.cpp
  src/AsulPackages/Os/Os.cpp
)

option(USE_READLINE "Enable readline support for REPL if available" ON)

if(USE_READLINE)
  # 首先尝试pkg-config
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(READLINE readline)

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
endif()

add_executable(alang ${SRCS})
target_include_directories(alang PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)

if(READLINE_FOUND)
  target_link_libraries(alang PRIVATE ${READLINE_LIBS})
elseif(DEFINED READLINE_LIBS)
  target_link_libraries(alang PRIVATE ${READLINE_LIBS})
endif()

if(WIN32)
  # Link Winsock for Windows networking support
  target_link_libraries(alang PRIVATE ws2_32)
  target_compile_definitions(alang PRIVATE _WIN32_WINNT=0x0600)
endif()

find_package(Threads REQUIRED)
target_link_libraries(alang PRIVATE Threads::Threads)

set_target_properties(alang PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

install(TARGETS alang RUNTIME DESTINATION bin)
