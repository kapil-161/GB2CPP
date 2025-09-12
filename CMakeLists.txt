cmake_minimum_required(VERSION 3.16)
project(GB2_CPP VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force static linking - only look for .a files (Windows/Linux only)
if(WIN32 OR UNIX AND NOT APPLE)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    set(BUILD_SHARED_LIBS OFF)
endif()

# Static runtime linking for MinGW  
if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
endif()

# Find required Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Charts)

# Force static library paths (Windows/Linux only)
if(WIN32 OR UNIX AND NOT APPLE)
    set_target_properties(Qt6::Core PROPERTIES
        IMPORTED_LOCATION "${Qt6_DIR}/../../../lib/libQt6Core.a"
    )
    set_target_properties(Qt6::Widgets PROPERTIES
        IMPORTED_LOCATION "${Qt6_DIR}/../../../lib/libQt6Widgets.a"
    )
    set_target_properties(Qt6::Charts PROPERTIES
        IMPORTED_LOCATION "${Qt6_DIR}/../../../lib/libQt6Charts.a"
    )
endif()

# Enable automatic processing of Qt MOC files
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files
set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/StatusWidget.cpp
    src/DataProcessor.cpp
    src/PlotWidget.cpp
    src/TableWidget.cpp
    src/MetricsCalculator.cpp
    src/MetricsTableWidget.cpp
    src/MetricsDialog.cpp
    src/PandasTableModel.cpp
    src/DataTableWidget.cpp
    src/CommandLineHandler.cpp
    src/SingleInstanceApp.cpp
    src/PlotSettingsDialog.cpp
)

# Header files
set(HEADERS
    include/MainWindow.h
    include/StatusWidget.h
    include/DataProcessor.h
    include/PlotWidget.h
    include/TableWidget.h
    include/Config.h
    include/MetricsCalculator.h
    include/MetricsTableWidget.h
    include/MetricsDialog.h
    include/PandasTableModel.h
    include/DataTableWidget.h
    include/CommandLineHandler.h
    include/SingleInstanceApp.h
    include/PlotSettingsDialog.h
)

# Add version resource for Windows
if(WIN32)
    set(RESOURCE_FILES resources/version.rc)
endif()

# Create executable
add_executable(GB2 ${SOURCES} ${HEADERS} ${RESOURCE_FILES})

# Link Qt6 libraries with static preference
target_link_libraries(GB2 Qt6::Core Qt6::Widgets Qt6::Charts)

# Platform-specific plugin imports and libraries
if(WIN32)
    # Import Qt6 plugins statically for Windows
    qt_import_plugins(GB2
        INCLUDE Qt6::QWindowsIntegrationPlugin Qt6::QICOPlugin Qt6::QJpegPlugin Qt6::QGifPlugin
        EXCLUDE Qt6::QXcbIntegrationPlugin Qt6::QCocoaIntegrationPlugin
    )
    
    # Link Windows system libraries
    target_link_libraries(GB2 
        # Windows system libraries
        ole32 oleaut32 imm32 winmm ws2_32 uuid
        # Graphics libraries  
        opengl32 gdi32 user32 shell32 advapi32
    )
elseif(APPLE)
    # Import Qt6 plugins for macOS
    qt_import_plugins(GB2
        INCLUDE Qt6::QCocoaIntegrationPlugin Qt6::QICOPlugin Qt6::QJpegPlugin Qt6::QGifPlugin
        EXCLUDE Qt6::QWindowsIntegrationPlugin Qt6::QXcbIntegrationPlugin
    )
    
    # Link macOS system frameworks
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(OPENGL_FRAMEWORK OpenGL)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    
    target_link_libraries(GB2 
        ${COCOA_FRAMEWORK}
        ${OPENGL_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
    )
else()
    # Linux/Unix plugins
    qt_import_plugins(GB2
        INCLUDE Qt6::QXcbIntegrationPlugin Qt6::QICOPlugin Qt6::QJpegPlugin Qt6::QGifPlugin
        EXCLUDE Qt6::QWindowsIntegrationPlugin Qt6::QCocoaIntegrationPlugin
    )
endif()

# Add static linking flags for Windows
if(WIN32)
    set_target_properties(GB2 PROPERTIES
        LINK_FLAGS "-Wl,--gc-sections -Wl,--strip-all"
    )
endif()

# Set output directory
set_target_properties(GB2 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Copy resources during build instead of configure time
add_custom_command(TARGET GB2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/resources ${CMAKE_BINARY_DIR}/bin/resources
)

# Platform-specific settings
if(WIN32)
    set_target_properties(GB2 PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    set_target_properties(GB2 PROPERTIES 
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE final.icns
        MACOSX_BUNDLE_BUNDLE_NAME "DSSAT GB2 Tool"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.dssat.gb2"
        MACOSX_BUNDLE_BUNDLE_VERSION "2.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "2.0"
    )
    
    # Copy icon file to app bundle
    set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/final.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
    target_sources(GB2 PRIVATE ${CMAKE_SOURCE_DIR}/resources/final.icns)
endif()