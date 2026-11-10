<p align="center"><img src="assets/MpOsc.png" alt="MpOsc" width="112"></p>

<h1 align="center">MpOsc</h1>

**Turn movement into live OSC.** Use a camera, a video, Syphon on macOS or Spout on Windows to play your visuals, sound and lighting with your face, hands and body. Tracking runs on your machine.

<!-- release-downloads:start -->
**[Download Mac](https://github.com/PixelStereo/MpOsc/releases/download/v0.2.1/MpOsc-macos-arm64-v0.2.1.zip)** · **[Download Windows](https://github.com/PixelStereo/MpOsc/releases/download/v0.2.1/MpOsc-windows-x64-v0.2.1-Setup.exe)**
<!-- release-downloads:end -->

On an **Apple Silicon Mac**, unzip and move MpOsc to Applications. On **Windows x64**, run the installer and launch MpOsc from the Start menu.

Choose a video source, enter your OSC destinations, click the signals you want, then turn on **Send OSC**. Save your setup as a `.mposc` file to recall your settings later.

<sub>Built by Pixel Stereo. Building from source? Initialize SDKs with <code>git submodule update --init</code>, then run <code>./build.sh</code>. Requires Qt 6, CMake, a C++17 compiler, and Python with <code>uv==0.9.28</code>.</sub>
