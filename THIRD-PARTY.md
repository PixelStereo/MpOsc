# Third-party components

MP_OSC reuses the MediaPipe worker, OSC codec and portable runtime packaging from
[NyxMap](https://github.com/PixelStereo/NyxMap) by Pixel Stereo. See `LICENSE` for
MP_OSC's GPL-3.0 license.

The Qt desktop runtime is deployed using Qt's deployment tools. The MediaPipe
runtime uses portable CPython from python-build-standalone, MediaPipe Tasks and
its Python dependencies. Python and the installed wheels retain their license
files and distribution metadata. The packaging script lists the versioned
Google model URLs and verifies each model against its SHA-256 before bundling it.
