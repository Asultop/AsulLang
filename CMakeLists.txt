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
)

option(USE_READLINE "Enable readline support for REPL if available" ON)

if(USE_READLINE)
  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(READLINE readline)
  endif()

  if(READLINE_FOUND)
    message(STATUS "readline found via pkg-config")
    include_directories(${READLINE_INCLUDE_DIRS})
    set(READLINE_LIBS ${READLINE_LIBRARIES})
    add_compile_definitions(USE_READLINE)
  else()
    # Common Homebrew locations
    if(EXISTS "/opt/homebrew/opt/readline/include/readline/readline.h")
      message(STATUS "readline headers found in /opt/homebrew/opt/readline")
      include_directories("/opt/homebrew/opt/readline/include")
      link_directories("/opt/homebrew/opt/readline/lib")
      set(READLINE_LIBS readline)
      add_compile_definitions(USE_READLINE)
    elseif(EXISTS "/usr/local/opt/readline/include/readline/readline.h")
      message(STATUS "readline headers found in /usr/local/opt/readline")
      include_directories("/usr/local/opt/readline/include")
      link_directories("/usr/local/opt/readline/lib")
      set(READLINE_LIBS readline)
      add_compile_definitions(USE_READLINE)
    else()
      message(STATUS "readline not found; REPL will fall back to getline")
    endif()
  endif()
endif()

add_executable(alang ${SRCS})
target_include_directories(alang PRIVATE ${CMAKE_SOURCE_DIR})

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
