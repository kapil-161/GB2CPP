# GB2 Troubleshooting Guide

## Build Issues

### ❌ Error: "/c: /c: Is a directory"
**Cause**: Wrong compiler or make tool paths
**Solution**: Use Qt's MinGW tools, not system MinGW
```bash
# Correct paths:
-DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe"
-DCMAKE_MAKE_PROGRAM="C:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe"
```

### ❌ Error: "Qt6 not found"
**Cause**: CMake can't locate Qt6 installation
**Solution**: Set CMAKE_PREFIX_PATH
```bash
-DCMAKE_PREFIX_PATH="C:/Qt/6.9.1/mingw_64"
```

### ❌ Error: "debug executable" detected by windeployqt
**Cause**: Missing release build type
**Solution**: Always specify release build
```bash
-DCMAKE_BUILD_TYPE=Release
```

## Deployment Issues

### ❌ Error: "Qt platform plugin could not be initialized"
**Cause**: Missing or incorrectly placed qwindows.dll
**Solutions**:
1. Check `platforms\qwindows.dll` exists in deployment folder
2. Verify folder structure preserved in Enigma Virtual Box
3. Test deployment folder before packaging:
```bash
cd manual_deployment
GB2.exe  # Should run without errors
```

### ❌ Error: Missing DLL dependencies
**Cause**: Incomplete windeployqt deployment
**Solution**: Run windeployqt with correct flags
```bash
windeployqt.exe --release --no-translations GB2.exe
```

### ❌ Error: Windows blocks executable
**Cause**: Self-extracting archives trigger security warnings
**Solution**: Use Enigma Virtual Box instead of batch/zip solutions

## Single Executable Issues

### ❌ Small file size (~1.6MB) but crashes
**Cause**: Enigma Virtual Box project missing dependencies
**Solution**: Manually add ALL files from deployment folder

### ❌ Large file size (~60MB) but still crashes
**Cause**: Plugin folder structure corrupted in packaging
**Solution**: 
1. Verify platforms\qwindows.dll in correct subfolder
2. Don't flatten directory structure in EVB
3. Test original deployment folder first

### ❌ Enigma Virtual Box "Project file is invalid"
**Cause**: Incorrect EVB XML format
**Solution**: Use GUI instead of command line, or fix XML syntax

## Quick Diagnostic Commands

### Test Build Environment
```bash
# Check CMake
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --version

# Check MinGW compiler
"C:\Qt\Tools\mingw1310_64\bin\g++.exe" --version

# Check Qt6 installation
dir "C:\Qt\6.9.1\mingw_64\bin\windeployqt.exe"
```

### Test Deployment
```bash
cd manual_deployment

# Test basic execution
GB2.exe --version

# Check critical files exist
dir platforms\qwindows.dll
dir Qt6Core.dll
dir Qt6Widgets.dll
dir Qt6Charts.dll
```

### Test Qt Platform Plugins
```bash
cd manual_deployment
set QT_DEBUG_PLUGINS=1
set QT_QPA_PLATFORM_PLUGIN_PATH=%cd%\platforms
GB2.exe
```

## File Size Reference

| Component | Expected Size | Notes |
|-----------|---------------|-------|
| GB2.exe | ~500KB | Main executable |
| Qt6 DLLs | ~40MB | Core Qt libraries |
| Plugins | ~5MB | Platform and format plugins |
| Runtime | ~2MB | MinGW runtime DLLs |
| System | ~5MB | D3D compiler, etc. |
| **Total** | **~60MB** | Complete deployment |

## Common Path Issues

Update these paths if Qt version changes:

```bash
# Qt 6.9.1 paths (current)
QT_ROOT=C:\Qt\6.9.1\mingw_64
CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe
MINGW=C:\Qt\Tools\mingw1310_64\bin

# If using Qt 6.8.0, update to:
QT_ROOT=C:\Qt\6.8.0\mingw_64
# CMAKE and MINGW paths usually stay the same
```

## Emergency Recovery

If build completely broken:

1. **Delete all build artifacts**:
```bash
rmdir /s /q build_win
rmdir /s /q manual_deployment
```

2. **Reset CMakeLists.txt**: Compare with backup version

3. **Clean Qt installation**: Use Qt Maintenance Tool to repair

4. **Use automation script**: Run `build_and_deploy.bat`

## Success Indicators

✅ **Build Success**:
- CMake configures without errors
- Build completes with "Built target GB2" message  
- `build_win\bin\GB2.exe` exists and is ~500KB

✅ **Deployment Success**:
- windeployqt runs without major errors
- `manual_deployment\` contains ~60MB of files
- `platforms\qwindows.dll` exists
- GB2.exe runs from deployment folder

✅ **Single Executable Success**:
- Enigma Virtual Box processes without errors
- Output file is ~60MB
- Single .exe runs on clean Windows system
- No "Qt platform plugin" errors