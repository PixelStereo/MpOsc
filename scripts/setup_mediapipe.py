#!/usr/bin/env python3
"""Install the local MediaPipe runtime and download versioned Google vision models."""
import argparse
import hashlib
import os
import shutil
import sys
from pathlib import Path
import subprocess
import tempfile
import urllib.request
import venv

BASE = "https://storage.googleapis.com/mediapipe-models/"
MODELS = {
    "hand_landmarker.task": ("hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task",
        "fbc2a30080c3c557093b5ddfc334698132eb341044ccee322ccf8bcf3607cde1"),
    "gesture_recognizer.task": ("gesture_recognizer/gesture_recognizer/float16/1/gesture_recognizer.task",
        "97952348cf6a6a4915c2ea1496b4b37ebabc50cbbf80571435643c455f2b0482"),
    "face_landmarker.task": ("face_landmarker/face_landmarker/float16/1/face_landmarker.task",
        "64184e229b263107bc2b804c6625db1341ff2bb731874b0bcc2fe6544e0bc9ff"),
    "pose_landmarker_lite.task": ("pose_landmarker/pose_landmarker_lite/float16/1/pose_landmarker_lite.task",
        "59929e1d1ee95287735ddd833b19cf4ac46d29bc7afddbbf6753c459690d574a"),
    "efficientdet_lite0.tflite": ("object_detector/efficientdet_lite0/float32/1/efficientdet_lite0.tflite",
        "40338edf5ec70d43e318b0a716a84d4564cd1802759a7a07170c7e43796dbf58"),
    "selfie_segmenter.tflite": ("image_segmenter/selfie_segmenter/float16/1/selfie_segmenter.tflite",
        "191ac9529ae506ee0beefa6b2c945a172dab9d07d1e802a290a4e4038226658b"),
    "blaze_face_short_range.tflite": ("face_detector/blaze_face_short_range/float16/1/blaze_face_short_range.tflite",
        "b4578f35940bf5a1a655214a1cce5cab13eba73c1297cd78e1a04c2380b0152f"),
    "efficientnet_lite0.tflite": ("image_classifier/efficientnet_lite0/float32/1/efficientnet_lite0.tflite",
        "6c7ab0a6e5dcbf38a8c33b960996a55a3b4300b36a018c4545801de3a3c8bde0"),
    "selfie_multiclass_256x256.tflite": ("image_segmenter/selfie_multiclass_256x256/float32/1/selfie_multiclass_256x256.tflite",
        "c6748b1253a99067ef71f7e26ca71096cd449baefa8f101900ea23016507e0e0"),
    "deeplab_v3.tflite": ("image_segmenter/deeplab_v3/float32/1/deeplab_v3.tflite",
        "ff36e24d40547fe9e645e2f4e8745d1876d6e38b332d39a82f0bf0f5d1d561b3"),
    "hair_segmenter.tflite": ("image_segmenter/hair_segmenter/float32/1/hair_segmenter.tflite",
        "2628cf3ce5f695f604cbea2841e00befcaa3624bf80caf3664bef2656d59bf84"),
    "selfie_segmenter_landscape.tflite": ("image_segmenter/selfie_segmenter_landscape/float16/1/selfie_segmenter_landscape.tflite",
        "490e9ea734313e0de10fa0cd9e3c6133e36ea4db2b7a49bde9ef019f72796b8e"),
}


def bundle_python(folder):
    """Stage a relocatable Python distribution and its native wheels."""
    runtime = folder / "python"
    if runtime.exists():
        raise RuntimeError(f"Bundle destination already contains Python: {runtime}")
    uv = [sys.executable, "-m", "uv"]
    with tempfile.TemporaryDirectory(prefix="nyxmap-python-") as directory:
        managed = Path(directory)
        env = {**os.environ, "UV_PYTHON_INSTALL_DIR": str(managed)}
        subprocess.run([*uv, "python", "install", "3.11", "--no-bin"], env=env, check=True)
        installs = list(managed.glob("cpython-3.11.*"))
        if len(installs) != 1:
            raise RuntimeError(f"Expected one portable Python installation, got {installs}")
        shutil.copytree(installs[0], runtime, symlinks=os.name != "nt")
        python = runtime / ("python.exe" if os.name == "nt" else "bin/python3.11")
        site = subprocess.check_output([str(python), "-I", "-c",
            "import sysconfig; print(sysconfig.get_path('purelib'))"], text=True).strip()
        dependencies = ["mediapipe==0.10.32"]
        if sys.platform == "darwin":
            # PyPI's macOS wheel contains FFmpeg codecs that cannot ship with NyxMap.
            # MediaPipe only uses OpenCV for drawing and encoding the setup preview.
            wheel_dir = managed / "wheelhouse"
            wheel_dir.mkdir()
            build_env = {
                **os.environ, "ENABLE_CONTRIB": "1", "ENABLE_HEADLESS": "0",
                "CMAKE_BUILD_PARALLEL_LEVEL": "4", "MACOSX_DEPLOYMENT_TARGET": "14.0",
                "CMAKE_ARGS": " ".join((
                    "-DBUILD_LIST=core,imgproc,imgcodecs,features2d,flann,python3",
                    "-DWITH_FFMPEG=OFF", "-DWITH_GSTREAMER=OFF", "-DWITH_AVFOUNDATION=OFF",
                    "-DWITH_QT=OFF", "-DWITH_GTK=OFF", "-DWITH_OPENCL=OFF", "-DWITH_IPP=OFF",
                    "-DBUILD_JPEG=ON", "-DBUILD_PNG=ON", "-DWITH_TIFF=OFF", "-DWITH_WEBP=OFF",
                    "-DWITH_OPENJPEG=OFF", "-DWITH_OPENEXR=OFF", "-DWITH_JASPER=OFF",
                    "-DWITH_AVIF=OFF", "-DWITH_JPEGXL=OFF", "-DWITH_GDAL=OFF",
                )),
            }
            subprocess.run([sys.executable, "-m", "pip", "wheel", "--no-deps", "--no-binary",
                            "opencv-contrib-python", "opencv-contrib-python==4.11.0.86",
                            "--wheel-dir", str(wheel_dir)], env=build_env, check=True)
            wheels = list(wheel_dir.glob("opencv_contrib_python-*.whl"))
            if len(wheels) != 1:
                raise RuntimeError(f"Expected exactly one locally built OpenCV wheel, got {wheels}")
            dependencies.append(str(wheels[0]))
        else:
            dependencies.append("opencv-contrib-python==5.0.0.93")
        subprocess.run([*uv, "pip", "install", "--python", str(python), "--target", site,
                        "--link-mode", "copy", "--only-binary", ":all:", *dependencies], check=True)
    if sys.platform.startswith("linux"):
        # Matplotlib uses Agg. The optional Tk module links to Tcl/Tk 9, absent
        # from the Ubuntu 24.04 release baseline.
        for extension in (runtime / "lib" / "python3.11" / "lib-dynload").glob("_tkinter*.so"):
            extension.unlink()
    return python


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--bundle", action="store_true", help="Stage portable Python and models for a release package")
    args = parser.parse_args()
    folder = args.directory.expanduser().resolve()
    folder.mkdir(parents=True, exist_ok=True)
    if args.bundle:
        python = bundle_python(folder)
    else:
        runtime = folder / ".venv"
        venv.EnvBuilder(with_pip=True, symlinks=os.name != "nt").create(runtime)
        python = runtime / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
        subprocess.run([str(python), "-m", "pip", "install", "mediapipe==0.10.32"], check=True)
    used_models = {"face_landmarker.task", "gesture_recognizer.task", "pose_landmarker_lite.task"}
    for filename, (url, digest) in MODELS.items():
        if filename not in used_models:
            continue
        destination = folder / filename
        if destination.is_file() and hashlib.sha256(destination.read_bytes()).hexdigest() == digest:
            continue
        with tempfile.NamedTemporaryFile(dir=folder, delete=False) as output:
            temporary = Path(output.name)
        try:
            checksum = hashlib.sha256()
            with urllib.request.urlopen(BASE + url, timeout=60) as response, temporary.open("wb") as output:
                while data := response.read(128 * 1024):
                    checksum.update(data)
                    output.write(data)
            if checksum.hexdigest() != digest:
                raise RuntimeError(f"Unexpected SHA-256 for {filename}")
            if args.bundle:
                temporary.chmod(0o644)
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)
    subprocess.run([str(python), "-c", "from mediapipe.tasks import python"], check=True,
                   env={**os.environ, "MPLBACKEND": "Agg"})
    print(f"MediaPipe model folder: {folder}")
    print(f"Local Python: {python}")


if __name__ == "__main__":
    main()
