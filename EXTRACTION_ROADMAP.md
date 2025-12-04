# Complete Extraction Roadmap

## Current State (Completed)
- ✅ Removed 803 lines of duplicate encoding/network code
- ✅ Created StdBuiltin package infrastructure (StdBuiltin.h/cpp)
- ✅ Extracted 3 simple global builtins (len, push, typeof)
- ✅ Updated build system (CMakeLists.txt, AsulPackages.h, AsulInterpreter.cpp)
- ✅ All 69 functional tests passing

## Remaining Work (Detailed Breakdown)

### 1. Complete StdBuiltin.cpp (~300 lines)
**Location in installBuiltins**: Lines 2834-3110
**Functions to extract**:
- `performance` (12 lines) - Simple, no `[this]` capture
- `quote` (148 lines) - Uses `[this]` for callFunction("eval")
- `push` (10 lines) - Already extracted, duplicate can be removed
- `typeof` (16 lines) - Already extracted, duplicate can be removed
- `eval` (74 lines) - Uses `[this]` extensively
- `sleep` (11 lines) - Uses `[this]` for settlePromise
- `Promise` (30 lines) - Uses `[this]` for settlePromise

**Challenge**: Functions with `[this]` need interpreter pointer passed as parameter

### 2. Extend StdIo.cpp (~530 lines)
**Location in installBuiltins**: Lines 2292-2821
**Functions/Classes to extract**:
- `print`, `println` (20 lines)
- `readFile`, `writeFile`, `appendFile`, `exists`, `listDir` (90 lines)
- `File` class with 11 methods (250 lines)
- `Dir` class with 7 methods (150 lines)
- `FileStream` class with 4 methods (70 lines)
- Filesystem operations: `mkdir`, `rmdir`, `stat`, `copy`, `move`, `chmod`, `walk` (200 lines)

**Challenge**: File.open() uses `[this]` to call ensurePackage

### 3. Create StdCollections.cpp (~600 lines)
**Location in installBuiltins**: Lines 3111-3640
**Classes/Functions to extract**:
- `Map` class (180 lines)
- `Set` class (150 lines)
- `Deque` class (100 lines)
- `Stack` class (80 lines)
- `PriorityQueue` class (150 lines)
- `binarySearch` function (40 lines)
- Utility functions: `keys`, `values`, `entries`, `clone`, `merge`, `range`, `enumerate`, `keysSorted` (150 lines)

**Challenge**: Some functions like sortBy use `[this]` for executeBlock

### 4. Update installBuiltins()
After extraction, should contain ONLY:
```cpp
void installBuiltins() {
    stdRoot = std::make_shared<Object>();
    globals->define("std", Value{stdRoot});
    globals->define("undefined", Value{std::monostate{}});
    packages["std"] = stdRoot;
    
    // Register all external packages
    registerExternalPackages(*this);
    
    // Import std.io symbols to global scope
    importPackageSymbols("std.io");
}
```

## Technical Challenges

### Handling `[this]` Captures
Functions that use `[this]` lambda captures need to be refactored to:
1. Capture a raw interpreter pointer: `Interpreter* interpPtr = &interp;`
2. Use the pointer in lambdas: `[interpPtr](...) { interpPtr->callFunction(...); }`

### Package Organization
- **std.io**: I/O functions go directly into std.io package
- **std.io.fileSystem**: File/Dir classes and filesystem operations
- **Global scope**: Builtins (len, push, typeof, eval, etc.) registered to globals
- **std.collections**: Collection classes and utilities

## Estimated Effort
- StdBuiltin completion: 3-4 hours (handling `[this]` captures carefully)
- StdIo extension: 4-5 hours (large volume of code, testing File/Dir/FileStream)  
- StdCollections creation: 3-4 hours (collections + utilities)
- Testing and debugging: 2-3 hours
**Total: 12-16 hours of focused development work**

## Next Steps
1. Complete StdBuiltin.cpp extraction (start with simple functions, then tackle `[this]` captures)
2. Extend StdIo.cpp (extract print/println first, then File class, then Dir, then FileStream)
3. Create StdCollections.cpp (extract collection classes one at a time)
4. Update installBuiltins() to minimal form
5. Run full test suite after each major extraction
6. Update CMakeLists.txt as needed

## Files That Will Be Modified
- `src/AsulPackages/Std/Builtin/StdBuiltin.cpp` - Add remaining builtins
- `src/AsulPackages/Std/Io/StdIo.cpp` - Add I/O functions and classes
- `src/AsulPackages/Std/Collections/StdCollections.h` - NEW FILE
- `src/AsulPackages/Std/Collections/StdCollections.cpp` - NEW FILE
- `src/AsulInterpreter.h` - Remove all inline implementations from installBuiltins()
- `src/AsulInterpreter.cpp` - Add registerStdCollectionsPackage() call
- `src/AsulPackages.h` - Add StdCollections.h include
- `CMakeLists.txt` - Add StdCollections.cpp to build

## Progress Tracking
This document serves as the roadmap. Each section should be checked off as completed.
The extraction must be done carefully and incrementally, with testing at each step.
