#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
AkiraOS Drone Detection — Model Training Script
================================================

Trains a small DS-CNN (Depthwise Separable CNN) audio classifier on
mel spectrograms extracted from drone/non-drone audio recordings.

The trained model can be exported as INT8 TFLite + a C array for direct
flashing to the STWINBX1 and loading via the inject/classify path.

Usage
-----
1. Prepare dataset:
   data/
     drone/       *.wav files recorded near a flying drone
     background/  *.wav files with urban/wind/speech background noise

2. Install dependencies:
   pip install tensorflow numpy librosa soundfile scikit-learn tqdm

3. Train:
   python train.py --data data/ --epochs 50 --output model/

4. Deploy INT8 TFLite to device:
   cp model/drone_detect_int8.tflite <target_lfs_mount>/models/drone_detect.tflite

Note: The embedded spectral energy classifier (drone_classify.c) is fully
functional without this model.  This script is for upgrading to a learned
neural network once you have labelled audio data.

Architecture
------------
Input:  (49, 40, 1) log mel spectrogram frame × 40 bins × 49 time steps
Model:  DS-CNN (small, ~25K parameters)
        Conv2D(32) → Depthwise+Pointwise ×3 → GlobalAvgPool → Dense(2) → Softmax
Output: [P(background), P(drone)]
"""

import argparse
import os
import sys
import json
from pathlib import Path

import numpy as np

# ── Optional heavy imports (skip if not available) ────────────────────────────

try:
    import librosa
    HAVE_LIBROSA = True
except ImportError:
    HAVE_LIBROSA = False

try:
    import tensorflow as tf
    HAVE_TF = True
except ImportError:
    HAVE_TF = False


# ── Audio parameters (must match firmware configuration) ─────────────────────

SAMPLE_RATE    = 16000
FFT_SIZE       = 512
HOP_SIZE       = 256
MEL_BINS       = 40
FRAMES         = 49
MEL_FREQ_MIN   = 50.0
MEL_FREQ_MAX   = 8000.0

# Mel spectrogram shape: (FRAMES, MEL_BINS)
INPUT_SHAPE = (FRAMES, MEL_BINS, 1)


# ── Feature extraction ────────────────────────────────────────────────────────

def extract_mel_spectrogram(wav_path: str) -> np.ndarray:
    """
    Extract log mel spectrogram segments from a WAV file.

    Returns a list of (FRAMES, MEL_BINS) arrays, one per non-overlapping
    segment.
    """
    if not HAVE_LIBROSA:
        raise ImportError("librosa is required: pip install librosa")

    y, sr = librosa.load(wav_path, sr=SAMPLE_RATE, mono=True)

    mel = librosa.feature.melspectrogram(
        y=y,
        sr=sr,
        n_fft=FFT_SIZE,
        hop_length=HOP_SIZE,
        n_mels=MEL_BINS,
        fmin=MEL_FREQ_MIN,
        fmax=MEL_FREQ_MAX,
        power=2.0,
    )

    # Log compression: log(1 + E)
    log_mel = np.log1p(mel)  # shape: (MEL_BINS, T)

    # Transpose to (T, MEL_BINS)
    log_mel = log_mel.T

    # Extract non-overlapping windows of FRAMES length
    segments = []
    n_frames = log_mel.shape[0]

    for start in range(0, n_frames - FRAMES + 1, FRAMES):
        seg = log_mel[start : start + FRAMES]  # (FRAMES, MEL_BINS)
        segments.append(seg)

    return segments


def load_dataset(data_dir: str):
    """
    Load all WAV files from data_dir/drone/ and data_dir/background/.

    Returns:
        X: numpy array of shape (N, FRAMES, MEL_BINS, 1)
        y: numpy array of shape (N,) with 0=background, 1=drone
    """
    data_dir = Path(data_dir)
    classes = {
        "background": 0,
        "drone":      1,
    }

    X_list, y_list = [], []

    for class_name, label in classes.items():
        class_dir = data_dir / class_name
        if not class_dir.exists():
            print(f"[WARN] Directory not found: {class_dir}")
            continue

        wav_files = list(class_dir.glob("*.wav"))
        print(f"Loading {len(wav_files)} files from {class_dir}...")

        for wav_path in wav_files:
            try:
                segs = extract_mel_spectrogram(str(wav_path))
                for seg in segs:
                    X_list.append(seg[..., np.newaxis])  # add channel dim
                    y_list.append(label)
            except Exception as exc:
                print(f"[WARN] Skipping {wav_path}: {exc}")

    if not X_list:
        raise ValueError("No data found. Check that data/drone/ and data/background/ exist.")

    X = np.array(X_list, dtype=np.float32)
    y = np.array(y_list, dtype=np.int32)

    print(f"Dataset: {X.shape[0]} samples  "
          f"(drone={np.sum(y==1)}, background={np.sum(y==0)})")
    return X, y


# ── Model definition ──────────────────────────────────────────────────────────

def build_ds_cnn(input_shape=INPUT_SHAPE) -> "tf.keras.Model":
    """
    Build a Depthwise Separable CNN for mel spectrogram classification.

    Architecture mimics 'DS-CNN-S' from Zhang et al., "Hello Edge: Keyword
    Spotting on Microcontrollers" (arXiv:1711.07128) scaled down for 40-bin
    mel input.

    Parameters:    ~25K
    Flash usage:   ~100 KB FP32, ~25 KB INT8 TFLite
    """
    if not HAVE_TF:
        raise ImportError("TensorFlow is required: pip install tensorflow")

    inputs = tf.keras.Input(shape=input_shape, name="mel_input")

    # Initial conv
    x = tf.keras.layers.Conv2D(32, (3, 3), padding="same", use_bias=False)(inputs)
    x = tf.keras.layers.BatchNormalization()(x)
    x = tf.keras.layers.ReLU()(x)

    # DS-CNN blocks
    for filters in [32, 64, 64]:
        x = tf.keras.layers.DepthwiseConv2D((3, 3), padding="same", use_bias=False)(x)
        x = tf.keras.layers.BatchNormalization()(x)
        x = tf.keras.layers.ReLU()(x)
        x = tf.keras.layers.Conv2D(filters, (1, 1), use_bias=False)(x)
        x = tf.keras.layers.BatchNormalization()(x)
        x = tf.keras.layers.ReLU()(x)

    x = tf.keras.layers.GlobalAveragePooling2D()(x)
    x = tf.keras.layers.Dropout(0.25)(x)
    outputs = tf.keras.layers.Dense(2, activation="softmax", name="class_output")(x)

    return tf.keras.Model(inputs, outputs, name="drone_detect_dscnn")


# ── INT8 quantisation ─────────────────────────────────────────────────────────

def export_int8_tflite(model, representative_data, output_path: str):
    """
    Export Keras model to INT8 TFLite using full-integer quantisation.

    The resulting .tflite file can be loaded directly into the TFLM runner on
    the STWINBX1 once TFLM module support is added (see docs/drone-detection.md).
    """
    if not HAVE_TF:
        raise ImportError("TensorFlow is required")

    def rep_data_gen():
        for sample in representative_data[:200]:
            yield [sample[np.newaxis, ...].astype(np.float32)]

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = rep_data_gen
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type  = tf.int8
    converter.inference_output_type = tf.int8

    tflite_model = converter.convert()

    with open(output_path, "wb") as f:
        f.write(tflite_model)

    print(f"INT8 TFLite model saved to: {output_path} ({len(tflite_model)} bytes)")
    return tflite_model


def export_c_array(tflite_bytes: bytes, output_path: str,
                   array_name: str = "g_drone_detect_model"):
    """
    Export TFLite model bytes as a C header array for embedding in firmware.

    The generated header can replace the LittleFS file load in tflm_runner.c.
    """
    with open(output_path, "w") as f:
        f.write("/* Auto-generated by AkiraSDK/python/drone_detect/train.py */\n")
        f.write("/* DO NOT EDIT */\n\n")
        f.write(f"#ifndef DRONE_DETECT_MODEL_DATA_H_\n")
        f.write(f"#define DRONE_DETECT_MODEL_DATA_H_\n\n")
        f.write(f"#include <stdint.h>\n\n")
        f.write(f"static const uint8_t {array_name}[] = {{\n")

        for i, byte in enumerate(tflite_bytes):
            if i % 16 == 0:
                f.write("  ")
            f.write(f"0x{byte:02x}, ")
            if (i + 1) % 16 == 0:
                f.write("\n")

        f.write("\n};\n\n")
        f.write(f"static const unsigned int {array_name}_len = {len(tflite_bytes)};\n\n")
        f.write(f"#endif /* DRONE_DETECT_MODEL_DATA_H_ */\n")

    print(f"C array header saved to: {output_path}")


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="AkiraOS Drone Detection Model Trainer"
    )
    parser.add_argument("--data",    default="data/",    help="Dataset root directory")
    parser.add_argument("--output",  default="model/",   help="Output directory")
    parser.add_argument("--epochs",  type=int, default=50, help="Training epochs")
    parser.add_argument("--batch",   type=int, default=32, help="Batch size")
    parser.add_argument("--lr",      type=float, default=1e-3, help="Learning rate")
    parser.add_argument("--val",     type=float, default=0.2, help="Validation split")
    parser.add_argument("--no-train", action="store_true",
                        help="Skip training (only export existing model)")
    args = parser.parse_args()

    if not HAVE_LIBROSA:
        print("[ERROR] librosa not found. Install: pip install librosa")
        sys.exit(1)
    if not HAVE_TF:
        print("[ERROR] TensorFlow not found. Install: pip install tensorflow")
        sys.exit(1)

    os.makedirs(args.output, exist_ok=True)

    print("=" * 60)
    print("AkiraOS Drone Detection — Training")
    print(f"  Sample rate : {SAMPLE_RATE} Hz")
    print(f"  FFT size    : {FFT_SIZE}")
    print(f"  Mel bins    : {MEL_BINS}")
    print(f"  Frames      : {FRAMES}")
    print(f"  Input shape : {INPUT_SHAPE}")
    print("=" * 60)

    # Load dataset
    X, y = load_dataset(args.data)

    # Build model
    model = build_ds_cnn()
    model.summary()

    keras_path = str(Path(args.output) / "drone_detect.keras")
    tflite_path = str(Path(args.output) / "drone_detect_int8.tflite")
    header_path = str(Path(args.output) / "drone_detect_model_data.h")

    if not args.no_train:
        # One-hot encode labels
        y_cat = tf.keras.utils.to_categorical(y, num_classes=2)

        # Class weights to handle imbalanced datasets
        n_bg   = int(np.sum(y == 0))
        n_dr   = int(np.sum(y == 1))
        n_total = len(y)
        class_weight = {
            0: n_total / (2.0 * max(n_bg, 1)),
            1: n_total / (2.0 * max(n_dr, 1)),
        }

        # Compile
        model.compile(
            optimizer=tf.keras.optimizers.Adam(learning_rate=args.lr),
            loss="categorical_crossentropy",
            metrics=["accuracy"],
        )

        # Train
        callbacks = [
            tf.keras.callbacks.EarlyStopping(patience=10, restore_best_weights=True),
            tf.keras.callbacks.ReduceLROnPlateau(factor=0.5, patience=5),
        ]

        history = model.fit(
            X, y_cat,
            epochs=args.epochs,
            batch_size=args.batch,
            validation_split=args.val,
            class_weight=class_weight,
            callbacks=callbacks,
        )

        # Save training history
        history_path = str(Path(args.output) / "training_history.json")
        with open(history_path, "w") as f:
            json.dump({k: [float(v) for v in vs]
                       for k, vs in history.history.items()}, f, indent=2)
        print(f"Training history saved to {history_path}")

        # Save Keras model
        model.save(keras_path)
        print(f"Keras model saved to {keras_path}")

    else:
        if os.path.exists(keras_path):
            model = tf.keras.models.load_model(keras_path)
            print(f"Loaded existing model from {keras_path}")
        else:
            print(f"[ERROR] No model found at {keras_path}. Run without --no-train first.")
            sys.exit(1)

    # Export INT8 TFLite
    tflite_bytes = export_int8_tflite(model, X, tflite_path)

    # Export C header
    export_c_array(tflite_bytes, header_path)

    print("\nDone!")
    print(f"\nTo deploy to STWINBX1:")
    print(f"  1. Copy {tflite_path} to /lfs/models/drone_detect.tflite")
    print(f"     (use AkiraOS file upload CLI or J-Link drag-and-drop)")
    print(f"  2. Or embed {header_path} in firmware and use")
    print(f"     drone_detect_use_embedded_model() API (future feature).")


if __name__ == "__main__":
    main()
