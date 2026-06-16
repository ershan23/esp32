import argparse
import json
import tempfile
from pathlib import Path

import numpy as np
import tensorflow as tf


LABELS = ["healthy", "disease"]


def parse_args():
    parser = argparse.ArgumentParser(description="Train a small int8 plant healthy/disease classifier.")
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--img-size", type=int, default=96)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--epochs", type=int, default=25)
    parser.add_argument("--seed", type=int, default=42)
    return parser.parse_args()


def dataset_dirs(dataset_root):
    train_dir = dataset_root / "train"
    val_dir = dataset_root / "validation"
    test_dir = dataset_root / "test"
    if not train_dir.exists():
        raise SystemExit(f"Missing {train_dir}. See tools/ml/README.md for dataset layout.")
    return train_dir, val_dir if val_dir.exists() else None, test_dir if test_dir.exists() else None


def load_dataset(directory, img_size, batch_size, shuffle, seed):
    ds = tf.keras.utils.image_dataset_from_directory(
        directory,
        labels="inferred",
        label_mode="categorical",
        class_names=LABELS,
        image_size=(img_size, img_size),
        batch_size=batch_size,
        shuffle=shuffle,
        seed=seed,
    )
    return ds.map(lambda x, y: (tf.cast(x, tf.float32) / 255.0, y), num_parallel_calls=tf.data.AUTOTUNE)


def make_model(img_size, batch_size=None):
    input_args = (
        {"batch_shape": (batch_size, img_size, img_size, 3)}
        if batch_size is not None
        else {"shape": (img_size, img_size, 3)}
    )
    return tf.keras.Sequential(
        [
            tf.keras.layers.Input(**input_args),
            tf.keras.layers.Conv2D(8, 3, activation="relu", padding="same"),
            tf.keras.layers.MaxPooling2D(),
            tf.keras.layers.Conv2D(16, 3, activation="relu", padding="same"),
            tf.keras.layers.MaxPooling2D(),
            tf.keras.layers.Conv2D(32, 3, activation="relu", padding="same"),
            tf.keras.layers.MaxPooling2D(),
            tf.keras.layers.Conv2D(32, 3, activation="relu", padding="same"),
            tf.keras.layers.MaxPooling2D(),
            tf.keras.layers.Reshape((6 * 6 * 32,)),
            tf.keras.layers.Dense(32, activation="relu"),
            tf.keras.layers.Dense(2, activation="softmax"),
        ]
    )


def representative_dataset(ds, max_batches=100):
    def gen():
        for index, (images, _) in enumerate(ds.unbatch().batch(1).take(max_batches)):
            yield [tf.cast(images, tf.float32)]
            if index + 1 >= max_batches:
                break

    return gen


def convert_float(model, output_path):
    with tempfile.TemporaryDirectory() as tmp_dir:
        model.export(tmp_dir)
        converter = tf.lite.TFLiteConverter.from_saved_model(tmp_dir)
        output_path.write_bytes(converter.convert())


def convert_int8(model, train_ds, output_path):
    with tempfile.TemporaryDirectory() as tmp_dir:
        model.export(tmp_dir)
        converter = tf.lite.TFLiteConverter.from_saved_model(tmp_dir)
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.representative_dataset = representative_dataset(train_ds)
        converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
        converter.inference_input_type = tf.int8
        converter.inference_output_type = tf.int8
        output_path.write_bytes(converter.convert())


def main():
    args = parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    tf.keras.utils.set_random_seed(args.seed)

    train_dir, val_dir, test_dir = dataset_dirs(args.dataset)
    train_ds = load_dataset(train_dir, args.img_size, args.batch_size, True, args.seed).prefetch(tf.data.AUTOTUNE)
    val_ds = load_dataset(val_dir, args.img_size, args.batch_size, False, args.seed).prefetch(tf.data.AUTOTUNE) if val_dir else None
    test_ds = load_dataset(test_dir, args.img_size, args.batch_size, False, args.seed).prefetch(tf.data.AUTOTUNE) if test_dir else None

    model = make_model(args.img_size)
    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=1e-3),
        loss="categorical_crossentropy",
        metrics=["accuracy"],
    )

    callbacks = [
        tf.keras.callbacks.EarlyStopping(monitor="val_accuracy" if val_ds else "accuracy", patience=5, restore_best_weights=True),
        tf.keras.callbacks.ReduceLROnPlateau(monitor="val_loss" if val_ds else "loss", patience=3, factor=0.5),
    ]

    history = model.fit(train_ds, validation_data=val_ds, epochs=args.epochs, callbacks=callbacks)
    eval_ds = test_ds or val_ds
    metrics = model.evaluate(eval_ds, verbose=0, return_dict=True) if eval_ds else {}

    keras_path = args.output / "plant_binary_model.keras"
    float_path = args.output / "plant_binary_float.tflite"
    int8_path = args.output / "plant_binary_int8.tflite"
    labels_path = args.output / "labels.txt"
    metadata_path = args.output / "metadata.json"

    model.save(keras_path)
    export_model = make_model(args.img_size, batch_size=1)
    export_model.set_weights(model.get_weights())
    convert_float(export_model, float_path)
    convert_int8(export_model, train_ds, int8_path)
    labels_path.write_text("\n".join(LABELS) + "\n", encoding="utf-8")

    metadata = {
        "labels": LABELS,
        "img_size": args.img_size,
        "input_shape": [1, args.img_size, args.img_size, 3],
        "input_dtype": "int8",
        "output_shape": [1, 2],
        "output_dtype": "int8",
        "metrics": metrics,
        "history_last": {key: float(values[-1]) for key, values in history.history.items()},
        "int8_model_bytes": int8_path.stat().st_size,
        "float_model_bytes": float_path.stat().st_size,
    }
    metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
