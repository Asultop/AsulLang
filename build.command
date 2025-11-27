#!/usr/bin/env bash
set -euo pipefail

# build.command - macOS build script (bash)
# Usage: double-click this file in Finder or run `./build.command [debug|clean]`

echo "Building ALang Engine (macOS build)..."

# Allow override
CXX=${CXX:-}
if [ -z "$CXX" ]; then
  if command -v clang++ >/dev/null 2>&1; then CXX=clang++;
  elif command -v g++ >/dev/null 2>&1; then CXX=g++;
  else
    echo "No C++ compiler found (clang++/g++ required)" >&2; exit 1;
  fi
fi

# Default flags (kept similar to build.sh)
CXXFLAGS="-std=c++17 -O2"

# Parse arguments: support -j/--jobs, debug, clean, --use-pch (placeholder)
MODE="build"
USE_PCH=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    -j|--jobs)
      JOBS="$2"; shift 2;;
    --jobs=*)
      JOBS="${1#*=}"; shift;;
    debug)
      MODE="debug"; shift;;
    clean)
      MODE="clean"; shift;;
    --use-pch)
      USE_PCH=1; shift;;
    *)
      # ignore unknown
      shift;;
  esac
done

if [ "$MODE" = "debug" ]; then
  CXXFLAGS="-std=c++17 -g -O0"
fi

OBJDIR="build/obj"
mkdir -p "$OBJDIR"

# Source files (explicit for deterministic build)
# Include Console.cpp so the REPL front-end is built into the executable
SRCS=(ALangEngine.cpp Console.cpp Main.cpp)

# Determine number of parallel jobs (macOS uses sysctl)

# Determine number of parallel jobs (macOS uses sysctl)
JOBS=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 1)}

REAL_CXX="$CXX"
# Detect and enable ccache if available (compile-time cache)
if command -v ccache >/dev/null 2>&1; then
  echo "ccache found — enabling ccache for compilation"
  CXX_CMD="ccache $REAL_CXX"
else
  CXX_CMD="$REAL_CXX"
fi

echo "Using compiler: $REAL_CXX"
echo "Compiler wrapper: $CXX_CMD"
echo "Jobs: $JOBS"

# Detect readline (pkg-config preferred). If found, add -DUSE_READLINE and link flags.
LINK_LIBS=""
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists readline 2>/dev/null; then
  echo "readline detected via pkg-config"
  RL_CFLAGS=$(pkg-config --cflags readline)
  RL_LIBS=$(pkg-config --libs readline)
  CXXFLAGS="$CXXFLAGS $RL_CFLAGS -DUSE_READLINE"
  LINK_LIBS="$RL_LIBS"
else
  # Common Homebrew locations
  if [ -f /opt/homebrew/opt/readline/include/readline/readline.h ]; then
    echo "readline headers found in /opt/homebrew/opt/readline"
    CXXFLAGS="$CXXFLAGS -I/opt/homebrew/opt/readline/include -DUSE_READLINE"
    LINK_LIBS="$LINK_LIBS -L/opt/homebrew/opt/readline/lib -lreadline"
  elif [ -f /usr/local/opt/readline/include/readline/readline.h ]; then
    echo "readline headers found in /usr/local/opt/readline"
    CXXFLAGS="$CXXFLAGS -I/usr/local/opt/readline/include -DUSE_READLINE"
    LINK_LIBS="$LINK_LIBS -L/usr/local/opt/readline/lib -lreadline"
  else
    echo "readline not detected; REPL will use simple getline (no arrow keys/history)."
  fi
fi

COMPILE_PIDS=()

prune_pids() {
  local -a new=()
  for pid in "${COMPILE_PIDS[@]:-}"; do
    if kill -0 "$pid" 2>/dev/null; then
      new+=("$pid")
    else
      # reap finished pid
      wait "$pid" 2>/dev/null || true
    fi
  done
  COMPILE_PIDS=("${new[@]}")
}

for src in "${SRCS[@]}"; do
  obj="$OBJDIR/${src%.cpp}.o"
  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
    echo "Compiling $src -> $obj"
    # Use CXX_CMD for compilation (may be ccache wrapper). Use -I. to ensure local headers found.
    $CXX_CMD -c $CXXFLAGS -I. "$src" -o "$obj" &
    COMPILE_PIDS+=("$!")
    # throttle by pruning finished background pids until we have capacity
    while [ "${#COMPILE_PIDS[@]}" -ge "$JOBS" ]; do
      prune_pids
      sleep 0.05
    done
  else
    echo "Up-to-date: $src"
  fi
done

# wait for remaining background compiles
prune_pids
for pid in "${COMPILE_PIDS[@]:-}"; do
  wait "$pid" 2>/dev/null || true
done

OBJS=($OBJDIR/*.o)
echo "Linking -> alang"
"$REAL_CXX" $CXXFLAGS "${OBJS[@]}" $LINK_LIBS -o alang

echo "Build completed."

if [ "${1:-}" = "clean" ]; then
  echo "Cleaning build artifacts..."
  rm -rf "$OBJDIR" alang
  echo "Cleaned."
fi
