#!/usr/bin/env bash
# Build MP_OSC locally, stage the MediaPipe runtime, and launch the app.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

config=Release
run=1
build_dir=build
cmake_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug) config=Debug ;;
        --no-run) run=0 ;;
        --build-dir)
            [[ $# -ge 2 && -n "$2" ]] || { echo "Missing build directory" >&2; exit 2; }
            build_dir="$2"
            shift ;;
        -D*) cmake_args+=("$1") ;;
        -h|--help)
            echo "Usage: ./build.sh [--debug] [--no-run] [--build-dir DIRECTORY] [-DNAME=VALUE ...]"
            exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

platform=$(uname -s)
case "$platform" in
    Darwin)
        if [[ -z "${CMAKE_PREFIX_PATH:-}" ]] && command -v brew >/dev/null 2>&1 &&
           qt_prefix=$(brew --prefix qt 2>/dev/null) &&
           [[ -f "$qt_prefix/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
            cmake_args=(-DCMAKE_PREFIX_PATH="$qt_prefix" ${cmake_args[@]+"${cmake_args[@]}"})
        fi ;;
    MINGW*|MSYS*|CYGWIN*) ;;
    *) echo "This build script supports macOS and Windows with Git Bash." >&2; exit 1 ;;
esac

cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE="$config" ${cmake_args[@]+"${cmake_args[@]}"}
cmake --build "$build_dir" --config "$config" --parallel

if [[ "$platform" == Darwin ]]; then
    app="$build_dir/MP_OSC.app"
    [[ -d "$app" ]] || app="$build_dir/$config/MP_OSC.app"
    runtime="$app/Contents/Resources/mediapipe"
    runtime_python="$runtime/python/bin/python3.11"
else
    app="$build_dir/$config/MP_OSC.exe"
    [[ -f "$app" ]] || app="$build_dir/MP_OSC.exe"
    runtime="$(dirname "$app")/mediapipe"
    runtime_python="$runtime/python/python.exe"
fi

if [[ ! -f "$runtime_python" || ! -f "$runtime/face_landmarker.task" ||
      ! -f "$runtime/gesture_recognizer.task" || ! -f "$runtime/pose_landmarker_lite.task" ]]; then
    python_cmd="${MP_OSC_PYTHON:-python3}"
    if ! "$python_cmd" -m uv --version >/dev/null 2>&1; then
        echo "Install uv==0.9.28 in your Python environment or set MP_OSC_PYTHON to a Python with uv installed." >&2
        exit 1
    fi
    mkdir -p "$(dirname "$runtime")"
    staging=$(mktemp -d "${runtime}.XXXXXX")
    trap 'rm -rf "$staging"' EXIT
    "$python_cmd" scripts/setup_mediapipe.py --bundle --directory "$staging/runtime"
    if [[ -e "$runtime" ]]; then mv "$runtime" "$staging/previous"; fi
    if ! mv "$staging/runtime" "$runtime"; then
        if [[ -e "$staging/previous" ]]; then
            mv "$staging/previous" "$runtime" || trap - EXIT
        fi
        exit 1
    fi
fi

if [[ "$run" == 1 ]]; then
    if [[ "$platform" == Darwin ]]; then
        open "$app"
    else
        "$app" &
    fi
fi
