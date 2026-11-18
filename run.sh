#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
qt_cmake=/home/kwille/Qt/Tools/CMake/bin/cmake
"$qt_cmake" --preset qt-debug
"$qt_cmake" --build --preset qt-debug
exec ./build/appuart_tool "$@"
