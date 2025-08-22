# GB2 Build and Deployment Guide

## Common Issues and Solutions

### 1. Compiler Path Issues

**Problem**: CMake tries to use TDM-GCC which causes "/c: /c: Is a directory" errors
**Solution**: Always specify Qt's MinGW compiler explicitly

```bash
# Use Qt's MinGW compiler (not system MinGW)
cmake .. -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe" \
  -DCMAKE_C_COMPILER="C:/Qt/Tools/mingw1310_64/bin/gcc.exe" \
  -DCMAKE_MAKE_PROGRAM="C:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe" \
  -DCMAKE_PREFIX_PATH="C:/Qt/6.9.1/mingw_64"
```

### 2. Qt6 Not Found

**Problem**: CMake can't find Qt6 installation
**Solution**: Always set CMAKE_PREFIX_PATH to Qt6 installation

### 3. Wrong Build Type Detection

**Problem**: windeployqt detects executable as "debug" when it's actually release
**Solution**: Always specify `-DCMAKE_BUILD_TYPE=Release` explicitly

### 4. Qt Platform Plugin Errors

**Problem**: "no Qt platform plugin could be initialized" when running single executable
**Root Cause**: Qt requires `qwindows.dll` to be in `platforms\` subfolder
**Solution**: Preserve folder structure in Enigma Virtual Box

## Correct Build Process

### Step 1: Clean Build
```bash
cd "C:\DSSAT48\Tools\GBCpp"
rm -rf build_win && mkdir build_win
cd build_win
```

### Step 2: Configure with Correct Paths
```bash
"C:\Qt\Tools\CMake_64\bin\cmake.exe" .. -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe" \
  -DCMAKE_C_COMPILER="C:/Qt/Tools/mingw1310_64/bin/gcc.exe" \
  -DCMAKE_MAKE_PROGRAM="C:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe" \
  -DCMAKE_PREFIX_PATH="C:/Qt/6.9.1/mingw_64"
```

### Step 3: Build
```bash
"C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe"
```

### Step 4: Create Deployment
```bash
# Create clean deployment folder
rm -rf manual_deployment && mkdir manual_deployment

# Copy executable and resources
cp build_win/bin/GB2.exe manual_deployment/
cp -r resources manual_deployment/

# Deploy Qt dependencies (preserves folder structure)
"C:\Qt\6.9.1\mingw_64\bin\windeployqt.exe" --release --no-translations manual_deployment/GB2.exe

# Add system libraries if needed
cp "C:\Windows\System32\D3Dcompiler_47.dll" manual_deployment/ 2>/dev/null || true
```

## Enigma Virtual Box Best Practices

### Critical Requirements:
1. **Preserve Folder Structure**: Never flatten the directory structure
2. **platforms\qwindows.dll**: Must stay in platforms subfolder
3. **Add ALL Files**: Include every file and folder from deployment directory
4. **Input File**: Use the main GB2.exe as input file
5. **Compression**: Enable for smaller file size (~60MB result)

### Correct Folder Structure in Virtual Box:
```
GB2.exe                    (Input file)
├── Qt6*.dll              (Root level)
├── platforms/             (Subfolder - CRITICAL)
│   └── qwindows.dll
├── imageformats/          (Subfolder)
├── iconengines/           (Subfolder)  
├── styles/                (Subfolder)
├── tls/                   (Subfolder)
├── resources/             (Subfolder)
└── *.dll                  (Runtime libraries)
```

## Tools and Paths Reference

### Required Tools:
- **CMake**: `C:\Qt\Tools\CMake_64\bin\cmake.exe`
- **MinGW Compiler**: `C:\Qt\Tools\mingw1310_64\bin\g++.exe`
- **MinGW Make**: `C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe`
- **Qt6 Installation**: `C:\Qt\6.9.1\mingw_64`
- **windeployqt**: `C:\Qt\6.9.1\mingw_64\bin\windeployqt.exe`
- **Enigma Virtual Box**: `C:\Program Files (x86)\Enigma Virtual Box\enigmavb.exe`

### Qt6 Plugin Requirements:
- **platforms\qwindows.dll**: Windows platform support (CRITICAL)
- **imageformats\**: Image format support (PNG, JPG, SVG, etc.)
- **iconengines\**: Icon rendering support
- **styles\**: UI styling support
- **tls\**: Network security support

## Troubleshooting

### Build Issues:
1. **"/c: /c: Is a directory"**: Wrong compiler/make tool paths
2. **"Qt6 not found"**: Missing CMAKE_PREFIX_PATH
3. **"Debug executable detected"**: Missing CMAKE_BUILD_TYPE=Release

### Deployment Issues:
1. **"Qt platform plugin could not be initialized"**: Folder structure not preserved
2. **Missing DLLs**: Incomplete windeployqt deployment
3. **Windows blocks executable**: Use Enigma Virtual Box instead of self-extracting archives

### Single Executable Issues:
1. **Small file size (~1.6MB)**: Missing dependencies in EVB project
2. **Large file size but crashes**: Plugin folder structure corrupted
3. **Correct size (~60MB) and works**: Successful deployment

## Automation Script

Create `build_and_deploy.bat`:
```batch
@echo off
echo Building GB2 and creating single executable...

REM Clean build
cd /d "C:\DSSAT48\Tools\GBCpp"
rmdir /s /q build_win 2>nul
mkdir build_win
cd build_win

REM Configure
"C:\Qt\Tools\CMake_64\bin\cmake.exe" .. -G "MinGW Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe" ^
  -DCMAKE_C_COMPILER="C:/Qt/Tools/mingw1310_64/bin/gcc.exe" ^
  -DCMAKE_MAKE_PROGRAM="C:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe" ^
  -DCMAKE_PREFIX_PATH="C:/Qt/6.9.1/mingw_64"

REM Build
"C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe"

REM Deploy
cd ..
rmdir /s /q manual_deployment 2>nul
mkdir manual_deployment
copy build_win\bin\GB2.exe manual_deployment\
xcopy /E /I resources manual_deployment\resources
"C:\Qt\6.9.1\mingw_64\bin\windeployqt.exe" --release --no-translations manual_deployment\GB2.exe

echo.
echo Deployment ready in manual_deployment folder
echo Use Enigma Virtual Box to create single executable
pause
```

## Version Compatibility

This guide is tested with:
- **Qt Version**: 6.9.1
- **MinGW Version**: 13.1.0 
- **CMake Version**: 3.30.5
- **Enigma Virtual Box**: 11.30
- **Windows Version**: 10.0.26200

Update paths if using different versions.