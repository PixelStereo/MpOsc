"""Local MediaPipe Tasks worker. Stdout is reserved for one JSON reply per frame."""
import base64
from contextlib import suppress
import json
import os
import sys
from pathlib import Path

MODELS = {
    "hands": ("HandLandmarker", "hand_landmarker.task"),
    "gestures": ("GestureRecognizer", "gesture_recognizer.task"),
    "face": ("FaceLandmarker", "face_landmarker.task"),
    "pose": ("PoseLandmarker", "pose_landmarker_lite.task"),
    "objects": ("ObjectDetector", "efficientdet_lite0.tflite"),
    "segment": ("ImageSegmenter", "selfie_segmenter.tflite"),
    "face_detector": ("FaceDetector", "blaze_face_short_range.tflite"),
    "classification": ("ImageClassifier", "efficientnet_lite0.tflite"),
    "parts": ("ImageSegmenter", "selfie_multiclass_256x256.tflite"),
    "landscape": ("ImageSegmenter", "selfie_segmenter_landscape.tflite"),
    "hair_detail": ("ImageSegmenter", "hair_segmenter.tflite"),
    "deeplab": ("ImageSegmenter", "deeplab_v3.tflite"),
}


def reply(value):
    print(json.dumps(value, separators=(",", ":"), allow_nan=False), flush=True)


def run(folder):
    import mediapipe as mp
    import numpy as np

    vision = mp.tasks.vision
    tasks = {}
    delegates = {}
    cpu_only = set()
    backend = os.environ.get("NYXMAP_MEDIAPIPE_DELEGATE", "auto").lower()
    if backend not in ("auto", "cpu", "gpu"):
        raise ValueError("NYXMAP_MEDIAPIPE_DELEGATE must be auto, cpu or gpu")

    def points(landmarks):
        return [[p.x, p.y, p.z] for p in landmarks]

    def infer(task, rgba, annotated):
        if task not in tasks:
            class_name, filename = MODELS[task]
            model = folder / filename
            if not model.is_file():
                raise FileNotFoundError(f"Missing MediaPipe model: {filename}")
            options = {}
            if task in ("hands", "gestures"):
                options.update(num_hands=2, min_hand_detection_confidence=.5)
            elif task == "face":
                options.update(num_faces=1, output_face_blendshapes=True, output_facial_transformation_matrixes=True)
            elif task == "pose":
                options.update(num_poses=1)
            elif task == "objects":
                options.update(max_results=10, score_threshold=.5)
            elif task == "classification":
                options.update(max_results=5)
            elif class_name == "ImageSegmenter":
                options.update(output_category_mask=task == "parts", output_confidence_masks=True)
            create = getattr(vision, class_name).create_from_options
            options_type = getattr(vision, class_name + "Options")
            for delegate in (("cpu",) if task in cpu_only else ("gpu", "cpu") if backend == "auto" else (backend,)):
                options["base_options"] = mp.tasks.BaseOptions(
                    model_asset_path=str(model), delegate=getattr(mp.tasks.BaseOptions.Delegate, delegate.upper()))
                try:
                    tasks[task] = create(options_type(**options))
                except Exception as error:
                    if delegate != "gpu" or backend != "auto":
                        raise RuntimeError(f"MediaPipe {task} ({delegate.upper()}): {error}") from error
                else:
                    delegates[task] = delegate.upper()
                    break
        # Apple's GPU converter accepts RGBA ImageFrames, but aborts on RGB.
        if delegates[task] == "GPU" and sys.platform == "darwin":
            image = mp.Image(image_format=mp.ImageFormat.SRGBA, data=rgba)
        else:
            image = mp.Image(image_format=mp.ImageFormat.SRGB, data=np.ascontiguousarray(rgba[:, :, :3]))
        detector = tasks[task]
        if task in ("hands", "gestures"):
            result = detector.detect(image) if task == "hands" else detector.recognize(image)
            output = {"hands": [
                {"points": points(hand), "world": points(result.hand_world_landmarks[i]), "score": result.handedness[i][0].score,
                 "right": result.handedness[i][0].category_name == "Right",
                 "gesture": result.gestures[i][0].category_name
                 if task == "gestures" and result.gestures[i] else ""}
                for i, hand in enumerate(result.hand_landmarks)
            ]}
            if annotated is not None and result.hand_landmarks:
                def draw_hands(annotated):
                    for hand in result.hand_landmarks:
                        vision.drawing_utils.draw_landmarks(
                            annotated, hand, vision.HandLandmarksConnections.HAND_CONNECTIONS,
                            landmark_drawing_spec=vision.drawing_styles.get_default_hand_landmarks_style(),
                            connection_drawing_spec=vision.drawing_styles.get_default_hand_connections_style(),
                        )
                draw_hands(annotated)
            return output
        if task == "face":
            result = detector.detect(image)
            output = {"face": [
                {"points": points(face), "expressions": {p.category_name: p.score for p in result.face_blendshapes[i]},
                 "blendshapes": {p.index: p.score for p in result.face_blendshapes[i]},
                 "matrix": result.facial_transformation_matrixes[i].flatten().tolist()}
                for i, face in enumerate(result.face_landmarks)
            ]}
            if annotated is not None and result.face_landmarks:
                def draw_face(annotated):
                    connections = vision.FaceLandmarksConnections
                    for face in result.face_landmarks:
                        vision.drawing_utils.draw_landmarks(
                            annotated, face, connections.FACE_LANDMARKS_CONTOURS,
                            landmark_drawing_spec=None,
                            connection_drawing_spec=vision.drawing_styles.get_default_face_mesh_contours_style(),
                        )
                        vision.drawing_utils.draw_landmarks(
                            annotated, face, connections.FACE_LANDMARKS_LEFT_IRIS + connections.FACE_LANDMARKS_RIGHT_IRIS,
                            landmark_drawing_spec=None,
                            connection_drawing_spec=vision.drawing_styles.get_default_face_mesh_iris_connections_style(),
                        )
                draw_face(annotated)
            return output
        if task == "pose":
            result = detector.detect(image)
            if annotated is not None:
                for pose in result.pose_landmarks:
                    vision.drawing_utils.draw_landmarks(
                        annotated, pose, vision.PoseLandmarksConnections.POSE_LANDMARKS,
                        landmark_drawing_spec=vision.drawing_styles.get_default_pose_landmarks_style(),
                    )
            return {"pose": [{"points": points(pose), "world": points(result.pose_world_landmarks[i]),
                              "visibility": [p.visibility for p in pose]}
                             for i, pose in enumerate(result.pose_landmarks)]}
        if task in ("objects", "face_detector"):
            return {task: [
                {"box": [d.bounding_box.origin_x / image.width, d.bounding_box.origin_y / image.height,
                         d.bounding_box.width / image.width, d.bounding_box.height / image.height],
                 "score": d.categories[0].score, "label": d.categories[0].category_name}
                for d in detector.detect(image).detections if d.categories
            ]}
        if task == "classification":
            classifications = detector.classify(image).classifications
            return {"classification": [{"label": item.category_name, "score": item.score, "index": item.index}
                                       for head in classifications for item in head.categories]}
        def alpha_packet(key, values):
            alpha = np.clip(values, 0, 1)
            return {key: base64.b64encode(np.rint(alpha * 255).astype(np.uint8).tobytes()).decode("ascii"),
                    key + "_width": alpha.shape[1], key + "_height": alpha.shape[0]}

        result = detector.segment(image)
        if task == "parts":
            mask = result.category_mask.numpy_view()
            return {"parts": base64.b64encode(mask.tobytes()).decode("ascii"),
                    "parts_width": mask.shape[1], "parts_height": mask.shape[0],
                    **alpha_packet("parts_foreground", 1 - result.confidence_masks[0].numpy_view())}
        if task == "deeplab":
            return alpha_packet(task, 1 - result.confidence_masks[0].numpy_view())
        foreground = result.confidence_masks[-1].numpy_view()
        if task != "segment":
            return alpha_packet(task, foreground)
        return {"coverage": float(np.count_nonzero(foreground >= .5) / foreground.size),
                **alpha_packet("mask", foreground)}

    reply({"ready": True})
    for line in sys.stdin.buffer:
        try:
            width, height, mask = map(int, line.split())
            if width <= 0 or height <= 0:
                raise ValueError("Invalid frame size")
            data = sys.stdin.buffer.read(width * height * 4)
            if len(data) != width * height * 4:
                break
            rgba = np.frombuffer(data, dtype=np.uint8).reshape(height, width, 4)
            result = {}
            annotated = None
            if mask & (1 << 12):
                import cv2
                annotated = cv2.cvtColor(rgba, cv2.COLOR_RGBA2BGR)
            for bit, task in enumerate(MODELS):
                if mask & (1 << bit):
                    try:
                        result.update(infer(task, rgba, annotated if task != "hands" or not mask & 2 else None))
                        result[task + "_delegate"] = delegates[task]
                    except Exception as error:
                        if backend == "auto" and delegates.get(task) == "GPU":
                            cpu_only.add(task)
                            delegates.pop(task, None)
                            with suppress(Exception):
                                tasks.pop(task).close()
                            try:
                                result.update(infer(task, rgba, annotated if task != "hands" or not mask & 2 else None))
                                result[task + "_delegate"] = delegates[task]
                                continue
                            except Exception as fallback_error:
                                error = fallback_error
                        result[task + "_error"] = str(error)
            if annotated is not None:
                ok, jpeg = cv2.imencode(".jpg", annotated, [cv2.IMWRITE_JPEG_QUALITY, 88])
                if ok:
                    result["preview_jpeg"] = base64.b64encode(jpeg.tobytes()).decode("ascii")
            reply(result)
        except Exception as error:
            reply({"error": str(error)})


if __name__ == "__main__":
    try:
        run(Path(sys.argv[1]))
    except Exception as error:
        reply({"error": str(error)})
