<p align="center"><img src="assets/MpOsc.png" alt="MpOsc" width="112"></p>

<h1 align="center">MpOsc</h1>

**Turn movement into live OSC.** Use a camera, a video, Syphon on macOS or Spout on Windows to play your visuals, sound and lighting with your face, hands and body. Tracking runs on your machine.

<!-- release-downloads:start -->
**LATEST VERSION · [v0.3.2](https://github.com/PixelStereo/MpOsc/releases/tag/v0.3.2)**

[![macOS Apple Silicon](https://img.shields.io/badge/macOS-Apple_Silicon-6b7787?style=for-the-badge&logo=apple&logoColor=white&labelColor=1a1c20)](https://github.com/PixelStereo/MpOsc/releases/download/v0.3.2/MpOsc-macos-arm64-v0.3.2.zip)&nbsp;&nbsp;[![Windows Installer](https://img.shields.io/badge/Windows-Installer-2b6fb6?style=for-the-badge&logo=windows&logoColor=white&labelColor=1a1c20)](https://github.com/PixelStereo/MpOsc/releases/download/v0.3.2/MpOsc-windows-x64-v0.3.2-Setup.exe)
<!-- release-downloads:end -->

On an **Apple Silicon Mac**, unzip and move MpOsc to Applications. On **Windows x64**, run the installer and launch MpOsc from the Start menu.

Choose a video source, enter your OSC destinations, click the signals you want, then turn on **Send OSC**. Save your setup as a `.mposc` file to recall your settings later.

## Drive it from anywhere

MpOsc is an OSCQuery device like every other Pixel Stereo application: it announces itself on the network, and any OSCQuery client (Ossia Score, Chataigne, Kadran, your own script) finds it, reads its tree and drives it. Ports **47821** (OSC) and **47822** (OSCQuery), or the next free ones.

| Address | What |
|---|---|
| `/osc/send`, `/osc/destinations` | arm the output, say where it goes |
| `/detection/confidence`, `/detection/tracking`, `/detection/hands` | the detection thresholds |
| `/analysis/size` | the size of the copy the detection runs on |
| `/tracking/face`, `/tracking/hands`, `/tracking/body` | what to track |
| `/preview/mirror`, `/preview/landmarks` | the preview |
| `/video/stop`, `/video/camera/start` | the source |
| `/face/…`, `/hands/…`, `/body/…` | every signal MpOsc sends, listed so you know what to map |

Writing an address does exactly what a click does.

<sub>Built by Pixel Stereo. Building from source? Initialize SDKs with <code>git submodule update --init</code>, then run <code>./build.sh</code>. Requires Qt 6, CMake, a C++17 compiler, and Python with <code>uv==0.9.28</code>.</sub>

## License

AGPL-3.0, see [LICENSE](LICENSE).
