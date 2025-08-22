# GB2 - DSSAT Data Viewer (C++ Version)

A complete C++ port of the Python DSSAT Data Viewer application, built with Qt6 and modern C++ features.

## Features

- **Data Loading & Processing**: Load and process DSSAT output files with automatic data type detection
- **Interactive Data Table**: View, filter, and sort data with an advanced table widget
- **Advanced Plotting**: Create line plots, scatter plots, and combined visualizations
- **Export Capabilities**: Export plots and data to various formats
- **Modern UI**: Clean, responsive interface with customizable plot settings
- **Cross-Platform**: Runs on Windows, macOS, and Linux

## Architecture

### Core Components

- **MainWindow**: Primary application window with tabbed interface
- **DataProcessor**: Handles DSSAT file parsing and data processing
- **TableWidget**: Advanced data table with filtering and sorting
- **PlotWidget**: Interactive plotting with Qt Charts
- **StatusWidget**: Status messages with flash notifications
- **Config**: Centralized configuration and constants

### Key Features Ported from Python

- ✅ PyQt6 → Qt6 C++
- ✅ Pandas data processing → Custom C++ classes with STL containers
- ✅ PyQtGraph plotting → Qt Charts
- ✅ File I/O and DSSAT format parsing
- ✅ Status notifications with flash effects
- ✅ Data table with filtering and export
- ✅ Modern UI styling and responsive layout

## Requirements

- **Qt6** (Core, Widgets, Charts)
- **CMake** 3.16 or higher
- **C++17** compatible compiler
- **DSSAT48** installation (for data files)

## Building

### Windows Quick Build

For Windows users, use the automated build script:

```batch
# Run the automated build and deployment script
build_and_deploy.bat
```

This script handles all compiler paths, Qt6 detection, and creates the deployment folder automatically.

### Manual Build Process

#### Prerequisites

- **Qt6 Installation**: Download from https://www.qt.io/download
- **MinGW Compiler**: Use Qt's bundled MinGW (recommended)
- **CMake**: Use Qt's bundled CMake tools

#### Step-by-Step Build

```bash
# 1. Clean previous build
rm -rf build_win && mkdir build_win && cd build_win

# 2. Configure with correct paths (CRITICAL - prevents compiler errors)
"C:\Qt\Tools\CMake_64\bin\cmake.exe" .. -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe" \
  -DCMAKE_C_COMPILER="C:/Qt/Tools/mingw1310_64/bin/gcc.exe" \
  -DCMAKE_MAKE_PROGRAM="C:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe" \
  -DCMAKE_PREFIX_PATH="C:/Qt/6.9.1/mingw_64"

# 3. Build
"C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe"
```

#### Troubleshooting

If you encounter build issues, see:
- **BUILD_NOTES.md** - Detailed build guide with solutions
- **TROUBLESHOOTING.md** - Common error fixes
- **build_and_deploy.bat** - Automated build script

### Alternative Build (Qt Creator)

1. Open `CMakeLists.txt` in Qt Creator
2. Configure with Qt6 MinGW kit (not system MinGW)
3. Ensure Release build configuration
4. Build and run

## Usage

### Basic Workflow

1. **Open Data File**: Use File → Open File to load DSSAT output files (.OUT, .CDE, .DAT)
2. **Select Variables**: Choose X and Y variables from the dropdown menus
3. **Configure Plot**: Select plot type (Line, Scatter, or Both) and customize appearance
4. **View Data**: Switch to the Data Table tab to examine the raw data
5. **Export**: Save plots as images or export data as CSV

### Supported File Formats

- **DSSAT Output Files**: .OUT files with simulation results
- **Observed Data**: .DAT files with experimental data
- **Generic Text**: Tab or space-delimited text files

### Plot Customization

- **Plot Types**: Line, Scatter, or Combined plots
- **Styling**: Adjustable line width, marker size, and colors
- **Layout**: Toggle legend and grid visibility
- **Export**: PNG, JPG formats supported

## File Structure

```
GBuild2/
├── CMakeLists.txt          # Build configuration
├── src/                    # Source files
│   ├── main.cpp           # Application entry point
│   ├── MainWindow.cpp     # Main application window
│   ├── DataProcessor.cpp  # Data loading and processing
│   ├── TableWidget.cpp    # Data table component
│   ├── PlotWidget.cpp     # Plotting component
│   └── StatusWidget.cpp   # Status notifications
├── include/               # Header files
│   ├── Config.h          # Application configuration
│   ├── MainWindow.h      # Main window interface
│   ├── DataProcessor.h   # Data processing interface
│   ├── TableWidget.h     # Table widget interface
│   ├── PlotWidget.h      # Plot widget interface
│   └── StatusWidget.h    # Status widget interface
└── resources/            # Application resources
    └── *.ico, *.png      # Application icons
```

## Configuration

### DSSAT Path Configuration

The application automatically detects DSSAT installation:
- **Windows**: `C:\DSSAT48`
- **macOS/Linux**: `/Applications/DSSAT48`

To customize, modify `Config.h` and rebuild.

### UI Customization

Styling is defined in `main.cpp` and can be customized by modifying the CSS-like stylesheet.

## Performance

The C++ version offers significant performance improvements over the Python original:

- **Faster Startup**: ~3x faster application launch
- **Memory Efficiency**: Lower memory usage for large datasets
- **Responsive UI**: Non-blocking operations with proper threading
- **Native Performance**: Platform-optimized Qt6 rendering

## Troubleshooting

### Common Issues

1. **Qt6 Not Found**: Ensure Qt6 is properly installed and `CMAKE_PREFIX_PATH` includes Qt6
2. **Missing Charts Module**: Install `qt6-charts-dev` package
3. **Icon Not Loading**: Verify `resources/` directory is copied to build output
4. **DSSAT Files Not Loading**: Check DSSAT installation path in `Config.h`

### Debug Build

For debugging, build with:
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

## Contributing

This is a complete port of the Python DSSAT viewer. The architecture maintains compatibility with the original while leveraging C++ performance and Qt6 capabilities.

### Code Style
- Modern C++17 features
- Qt6 best practices
- RAII and smart pointers
- Const-correctness

## License

Same license as the original DSSAT tools.

## Single Executable Distribution

The application can be packaged as a single portable executable using **Enigma Virtual Box**:

### Creating Single Executable

1. **Prepare deployment folder**: Use Qt's `windeployqt` to gather all dependencies:
   ```bash
   windeployqt --release --no-translations GB2.exe
   ```

2. **Use Enigma Virtual Box**:
   - Install Enigma Virtual Box (free tool)
   - Set Input File: `GB2.exe`
   - Add ALL files and folders from deployment directory
   - **CRITICAL**: Preserve folder structure (especially `platforms\qwindows.dll`)
   - Enable compression for smaller file size
   - Process to create single `.exe`

3. **Result**: ~63MB single executable that runs on any Windows system without Qt6 installation

### Deployment Structure Required

```
deployment/
├── GB2.exe                    # Main executable
├── Qt6*.dll                   # Qt6 libraries (Charts, Core, Gui, Widgets, etc.)
├── platforms/                 # MUST preserve this folder structure
│   └── qwindows.dll          # Windows platform plugin
├── imageformats/             # Image format plugins
├── iconengines/              # Icon engine plugins
├── styles/                   # Style plugins
├── tls/                      # TLS backend plugins
├── resources/                # Application resources
└── *.dll                     # Runtime libraries (libgcc, libstdc++, etc.)
```

**Note**: The folder structure is critical - Qt platform plugins must be in subdirectories for proper initialization.

## Changelog

### Version 2.1.0 (Single Executable)
- Added Enigma Virtual Box packaging support
- Created portable single executable distribution
- Resolved Qt platform plugin deployment issues
- Enhanced deployment documentation

### Version 2.0.0 (C++ Port)
- Complete rewrite in C++ with Qt6
- Enhanced performance and memory efficiency
- Modern UI with Qt Charts integration
- Improved data processing with STL containers
- Cross-platform compatibility maintained# GB2CPP
