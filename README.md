# MP_OSC

MP_OSC (MediaPipe OSC / Motion Processing OSC) turns camera or video movement into OSC controls. The Qt desktop app runs MediaPipe locally and ships with the Python runtime and models in the release packages.

## Perform

1. Choose a device and click **Camera**, or open a video file.
2. Choose **Face**, **Hands**, **Body**, or **Face + hands**. The Setup view shows the annotated source and live signal levels. Face exposes the blendshape scores. Hands tracks left and right simultaneously, including gestures. Body exposes XYZ landmarks.
3. Tick **Send** for each signal you want. Enter one or more IP and port pairs separated by whitespace, for example `127.0.0.1:5000 10.0.0.2:9999`.
4. Enable **Send OSC**. Each checked signal is sent to every destination. The address appears in the signal list.

**Smooth (ms)** is an optional time constant, **Deadband** suppresses changes smaller than the chosen amount, and **Threshold** converts a continuous value into a 0/1 gate. Each filter starts off. OSC values are floats. IPv6 addresses can use brackets, for example `[::1]:5000`.

## Build and release

Install Qt 6 with Widgets, Network and Multimedia, CMake, and a C++17 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

For a source build, stage the same portable runtime as the releases. Use Python 3.11 with `uv==0.9.28` installed and run:

```sh
python scripts/setup_mediapipe.py --bundle --directory build/MP_OSC.app/Contents/Resources/mediapipe
```

On Windows with the Visual Studio generator, use `build/Release/mediapipe` as the destination. MediaPipe inference works offline after packaging. On macOS, grant camera access when the system prompts. The macOS CI archives use ad-hoc signing.

GitHub Actions builds macOS Apple Silicon and Windows x64, packages the portable MediaPipe runtime, and runs inference from the extracted archives. Pushing a tag such as `v0.1.0` publishes both ZIPs to GitHub Releases once both checks pass. Linux, Syphon, Spout, NDI and OMT are outside this first version.
