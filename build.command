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
CXXFLAGS="-std=c++17 -O2 "

if [ "${1:-}" = "debug" ]; then
  CXXFLAGS="-std=c++17 -g -O0"
fi

OBJDIR="build/obj"
mkdir -p "$OBJDIR"

# Source files (explicit for deterministic build)
SRCS=(ALangEngine.cpp Main.cpp)

# Determine number of parallel jobs (macOS uses sysctl)

# Determine number of parallel jobs (macOS uses sysctl)
JOBS=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 1)}

echo "Using compiler: $CXX"
echo "Jobs: $JOBS"

COMPILE_PIDS=()

prune_pids() {
  local -a new=()
  for pid in "${COMPILE_PIDS[@]}"; do
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
    "$CXX" -c $CXXFLAGS "$src" -o "$obj" &
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
for pid in "${COMPILE_PIDS[@]}"; do
  wait "$pid" 2>/dev/null || true
done

OBJS=($OBJDIR/*.o)
echo "Linking -> alang"
"$CXX" $CXXFLAGS "${OBJS[@]}" -o alang

echo "Build completed."

if [ "${1:-}" = "clean" ]; then
  echo "Cleaning build artifacts..."
  rm -rf "$OBJDIR" alang
  echo "Cleaned."
fi
