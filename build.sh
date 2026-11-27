#!/usr/bin/env bash
# Build MpOsc locally, stage the MediaPipe runtime, and launch the app.
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

app_name=$(sed -n 's/.*"name":"\([^"]*\)".*/\1/p' "$build_dir/application.json")
[[ -n "$app_name" ]] || { echo "Missing application name" >&2; exit 1; }

if [[ "$platform" == Darwin ]]; then
    app="$build_dir/$app_name.app"
    [[ -d "$app" ]] || app="$build_dir/$config/$app_name.app"
    runtime="$app/Contents/Resources/mediapipe"
    runtime_python_path="python/bin/python3.11"
else
    app="$build_dir/$config/$app_name.exe"
    [[ -f "$app" ]] || app="$build_dir/$app_name.exe"
    runtime="$(dirname "$app")/mediapipe"
    runtime_python_path="python/python.exe"
fi

python_cmd="${MPOSC_PYTHON:-python3}"
cache="$build_dir/mediapipe-runtime"
if [[ ! -d "$cache" && "$platform" == Darwin && -d "$build_dir/MP_OSC.app/Contents/Resources/mediapipe/python" ]]; then
    cp -R "$build_dir/MP_OSC.app/Contents/Resources/mediapipe" "$cache"
fi
complete_mediapipe_runtime() {
    local directory="$1"
    [[ -x "$directory/$runtime_python_path" && -s "$directory/face_landmarker.task" &&
       -s "$directory/gesture_recognizer.task" && -s "$directory/pose_landmarker_lite.task" ]]
}
if ! complete_mediapipe_runtime "$runtime"; then
    if ! complete_mediapipe_runtime "$cache"; then
        uv_python="$python_cmd"
        if ! "$uv_python" -m uv --version >/dev/null 2>&1; then
            setup_env="$build_dir/mposc-setup"
            uv_python="$setup_env/bin/python"
            if [[ "$platform" != Darwin ]]; then uv_python="$setup_env/Scripts/python.exe"; fi
            if ! "$uv_python" -m uv --version >/dev/null 2>&1; then
                "$python_cmd" -m venv "$setup_env"
                "$uv_python" -m pip install uv==0.9.28
            fi
        fi
        scratch=$(mktemp -d "$build_dir/mediapipe.XXXXXX")
        trap 'rm -rf "$scratch"' EXIT
        "$uv_python" scripts/setup_mediapipe.py --bundle --directory "$scratch/runtime"
        rm -rf "$cache"
        mv "$scratch/runtime" "$cache"
    fi
    mkdir -p "$(dirname "$runtime")"
    staging=$(mktemp -d "${runtime}.XXXXXX")
    trap 'rm -rf "$staging" "${scratch:-}"' EXIT
    cp -R "$cache" "$staging/runtime"
    if [[ -e "$runtime" ]]; then mv "$runtime" "$staging/previous"; fi
    if ! mv "$staging/runtime" "$runtime"; then
        if [[ -e "$staging/previous" ]]; then
            mv "$staging/previous" "$runtime" || trap - EXIT
        fi
        exit 1
    fi
fi

if [[ "$platform" == Darwin ]]; then
    executable="$app/Contents/MacOS/$app_name"
    omt_folder="$app/Contents/Frameworks"
else
    executable="$app"
    omt_folder="$(dirname "$app")"
fi
"$python_cmd" scripts/setup_omt.py "$omt_folder" --cache "$build_dir/omt-cache"

if [[ "$run" == 1 ]]; then
    if ! NYXMAP_MEDIAPIPE_DELEGATE=cpu "$executable" --check-mediapipe; then
        echo "The built app could not run MediaPipe. See the diagnostic above." >&2
        exit 1
    fi
    if [[ "$platform" == Darwin ]]; then
        open -n "$app"
    else
        "$app" &
    fi
fi
