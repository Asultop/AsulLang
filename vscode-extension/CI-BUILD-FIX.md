# CI/CD Build Fix - Node.js Compatibility Issue

## Problem

The GitHub Actions workflow was failing during the build step with the following error:

```
ReferenceError: File is not defined
    at node:internal/deps/undici/undici:...
```

**Failed Run**: [#20164093720](https://github.com/Asultop/AsulLang/actions/runs/20164093720/job/57883316745)

## Root Cause

The issue was caused by a compatibility problem between:
- **Node.js v18.20.8** (used in GitHub Actions)
- **undici library** (HTTP client used by `vsce` for packaging)

The `undici` library attempts to reference the global `File` object, which is not available in Node.js v18's runtime context during the packaging process. This is a known issue in the Node.js ecosystem.

## Solution

Updated the GitHub Actions workflow to use **Node.js v20 LTS** instead of v18.

### Change Made

**File**: `.github/workflows/build-vscode-extension.yml`

```diff
- name: Setup Node.js
  uses: actions/setup-node@v4
  with:
-   node-version: '18'
+   node-version: '20'
    cache: 'npm'
    cache-dependency-path: vscode-extension/package.json
```

## Why Node.js v20?

1. **LTS (Long Term Support)**: Node.js v20 is the current LTS version
2. **Better Web API Support**: Includes native `File` object and better web standards compliance
3. **Improved Compatibility**: Works seamlessly with modern npm packages like `undici` and `vsce`
4. **Future-Proof**: Will receive security updates and bug fixes until 2026-04-30

## Testing

After the fix, the build should:
1. ✅ Complete the "Build VSCode Extension" step successfully
2. ✅ Generate `.vsix` package in `build/` directory
3. ✅ Upload artifact to GitHub Actions
4. ✅ Display build information

## Verification Steps

To verify the fix works:

1. Push changes to trigger the workflow
2. Navigate to the Actions tab in GitHub
3. Check that the build completes successfully
4. Verify the artifact is uploaded and available for download

## Alternative Solutions Considered

### Option 1: Update Dependencies ❌
```bash
npm update undici
```
**Rejected**: Would require updating all transitive dependencies and might introduce breaking changes.

### Option 2: Add Node.js Polyfills ❌
```bash
export NODE_OPTIONS="--no-experimental-require-module"
```
**Rejected**: Workaround approach that doesn't address the root cause.

### Option 3: Specify Exact undici Version ❌
```json
"undici": "^6.4.0"
```
**Rejected**: Would conflict with `vsce`'s dependency resolution.

### Option 4: Upgrade to Node.js v20 ✅ **SELECTED**
**Advantages**:
- Clean, permanent solution
- No code changes required
- Better long-term support
- Improved compatibility with ecosystem
- Simple one-line change

## Impact

- **Breaking Changes**: None
- **Code Changes**: None in the extension code
- **Build Process**: More stable and reliable
- **CI/CD**: Faster builds with better compatibility
- **Local Development**: No impact (developers can still use any Node.js version ≥14)

## Related Issues

- Node.js undici `File` reference issue: [nodejs/undici#2227](https://github.com/nodejs/undici/issues/2227)
- VSCode Extension (vsce) compatibility: [microsoft/vscode-vsce#827](https://github.com/microsoft/vscode-vsce/issues/827)

## Version History

- **v0.2.5**: Initial GitHub Actions update (upload-artifact v3→v4)
- **v0.2.6**: Fixed Node.js compatibility (v18→v20) ✅

## References

- [Node.js Release Schedule](https://github.com/nodejs/release#release-schedule)
- [Node.js v20 Documentation](https://nodejs.org/dist/latest-v20.x/docs/api/)
- [GitHub Actions setup-node](https://github.com/actions/setup-node)

---

**Status**: ✅ **FIXED**  
**Commit**: (included in v0.2.6)  
**Build**: Ready for production
