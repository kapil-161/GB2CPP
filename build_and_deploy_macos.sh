#!/bin/bash
# GB2 macOS Build and Deployment Script

echo "========================================"
echo "GB2 macOS Build and Deployment Script"
echo "========================================"
echo ""

# Set project directory
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_DIR"

# Auto-detect Qt installation
QT_DIR=""
if [ -d "/opt/homebrew/opt/qt@6" ]; then
    QT_DIR="/opt/homebrew/opt/qt@6"
elif [ -d "/usr/local/opt/qt@6" ]; then
    QT_DIR="/usr/local/opt/qt@6"
elif [ -d "$HOME/Qt/6.9.1/macos" ]; then
    QT_DIR="$HOME/Qt/6.9.1/macos"
elif [ -d "$HOME/Qt/6.8.1/macos" ]; then
    QT_DIR="$HOME/Qt/6.8.1/macos"
else
    echo "ERROR: Qt6 installation not found!"
    echo "Please install Qt6 via Homebrew: brew install qt@6"
    echo "Or download from https://www.qt.io/download"
    exit 1
fi

echo "Found Qt at: $QT_DIR"

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo "ERROR: CMake not found!"
    echo "Please install CMake: brew install cmake"
    exit 1
fi

# Check for macdeployqt
MACDEPLOYQT="$QT_DIR/bin/macdeployqt"
if [ ! -f "$MACDEPLOYQT" ]; then
    echo "ERROR: macdeployqt not found at $MACDEPLOYQT"
    exit 1
fi

echo ""
echo "Step 1: Cleaning previous build..."
if [ -d "build_macos" ]; then
    rm -rf build_macos
fi
mkdir -p build_macos
cd build_macos

echo ""
echo "Step 2: Configuring with CMake..."

cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_DIR" \
  -G "Unix Makefiles"

if [ $? -ne 0 ]; then
    echo "ERROR: CMake configuration failed!"
    exit 1
fi

echo ""
echo "Step 3: Building application..."
make -j$(sysctl -n hw.ncpu)

if [ $? -ne 0 ]; then
    echo "ERROR: Build failed!"
    exit 1
fi

echo ""
echo "Step 4: Deploying Qt6 dependencies..."

# Navigate to the app bundle location
cd bin

# Check if app bundle exists
if [ ! -d "GB2.app" ]; then
    echo "ERROR: GB2.app not found in build_macos/bin/"
    exit 1
fi

# Deploy Qt libraries using macdeployqt
"$MACDEPLOYQT" GB2.app -always-overwrite

if [ $? -ne 0 ]; then
    echo "ERROR: macdeployqt failed!"
    exit 1
fi

echo ""
echo "Step 5: Verifying deployment..."

# Check if Qt libraries are present
if [ -d "GB2.app/Contents/Frameworks/QtCharts.framework" ]; then
    echo "✓ QtCharts framework found"
else
    echo "WARNING: QtCharts framework not found!"
fi

if [ -d "GB2.app/Contents/Frameworks/QtCore.framework" ]; then
    echo "✓ QtCore framework found"
fi

if [ -d "GB2.app/Contents/Frameworks/QtWidgets.framework" ]; then
    echo "✓ QtWidgets framework found"
fi

echo ""
echo "========================================"
echo "SUCCESS: macOS build and deployment complete!"
echo "========================================"
echo ""
echo "App bundle location: $PROJECT_DIR/build_macos/bin/GB2.app"
echo ""
echo "To run the application:"
echo "  open build_macos/bin/GB2.app"
echo ""
echo "To create a distributable package:"
echo "  1. Right-click GB2.app in Finder"
echo "  2. Select 'Compress' to create a .zip"
echo "  3. Or use: hdiutil create -srcfolder GB2.app GB2.dmg"
echo ""
echo "========================================"

