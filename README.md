# MP_OSC

MP_OSC (MediaPipe OSC / Motion Processing OSC) turns camera or video movement into OSC controls. The Qt desktop app runs MediaPipe locally and ships with the Python runtime and models in the release packages.

## Perform

1. Choose a device and click **Camera**, or open a video file.
2. Enable **Face**, **Hands**, and **Body** independently. Enable all three to track them on the same frame. The Setup view shows the annotated source and live signal levels. Face exposes the blendshape scores. Hands tracks left and right simultaneously, including gestures. Body exposes XYZ landmarks.
3. Click each signal name to enable it. An outline marks enabled signals. Enter one or more IP and port pairs separated by whitespace, for example `127.0.0.1:5000 10.0.0.2:9999`.
4. Enable **Send OSC**. Each enabled signal is sent to every destination. The address appears in the signal list.

**Attack** sets the smoothing time for rising values and **Release** sets it for falling values. Both are in seconds. A value of zero follows the input immediately. OSC values are floats. IPv6 addresses can use brackets, for example `[::1]:5000`.

Click the pin next to a signal to move it to the top. Click again to unpin it. **Save .mposc…** exports the interface and OSC settings, including per-signal settings and pins. **Open .mposc…** restores them. Start the camera or open a video to resume tracking.

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
