#!/bin/bash
#!/usr/bin/env bash
set -euo pipefail

echo "Building ALang Engine (smart build)..."

# Allow override
CXX=${CXX:-}
if [ -z "$CXX" ]; then
	if command -v g++ >/dev/null 2>&1; then CXX=g++;
	elif command -v clang++ >/dev/null 2>&1; then CXX=clang++;
	else
		echo "No C++ compiler found (g++/clang++ required)" >&2; exit 1;
	fi
fi

CXXFLAGS="-std=c++17 -O2 -fexec-charset=GBK"

OBJDIR="build/obj"
mkdir -p "$OBJDIR"

# Source files (explicit list keeps build deterministic)
SRCS=(ALangEngine.cpp Main.cpp)

# Parallel jobs
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 1)}

COMPILE_JOBS=()
for src in "${SRCS[@]}"; do
	obj="$OBJDIR/${src%.cpp}.o"
	if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
		echo "Compiling $src -> $obj"
		# compile in background to utilize multiple cores
		"$CXX" -c $CXXFLAGS "$src" -o "$obj" &
		COMPILE_JOBS+=("$!")
		# throttle
		while [ "${#COMPILE_JOBS[@]}" -ge "$JOBS" ]; do
			wait -n || true
			# prune finished pids
			tmp=(); for pid in "${COMPILE_JOBS[@]}"; do kill -0 "$pid" 2>/dev/null && tmp+=("$pid") || true; done; COMPILE_JOBS=("${tmp[@]}")
		done
	else
		echo "Up-to-date: $src"
	fi
done

# wait for remaining
wait || true

# Link
OBJS=($OBJDIR/*.o)
echo "Linking -> alang"
"$CXX" $CXXFLAGS "${OBJS[@]}" -o alang

echo "Build completed."

if [ "${1:-}" = "clean" ]; then
	echo "Cleaning build artifacts..."
	rm -rf "$OBJDIR" alang
	echo "Cleaned."
fi