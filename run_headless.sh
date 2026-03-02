#!/bin/bash
export PATH="C:/Qt/Tools/mingw1310_64/bin:/usr/bin:$PATH"
export QTFRAMEWORK_BYPASS_LICENSE_CHECK=1
export QT_QPA_PLATFORM=offscreen
export QT_QPA_FONTDIR="C:/Windows/Fonts"

cd "C:/DSSAT48/Tools/GB2CPP/manual_deployment"

echo "Running GB2..."
./GB2.exe "$@"

EXIT_CODE=$?
echo ""
echo "EXIT CODE: $EXIT_CODE"
exit $EXIT_CODE
