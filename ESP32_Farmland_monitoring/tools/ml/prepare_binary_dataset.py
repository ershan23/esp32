import argparse
import random
import shutil
from pathlib import Path


IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


def parse_args():
    parser = argparse.ArgumentParser(description="Convert class-folder leaf datasets into healthy/disease splits.")
    parser.add_argument("--source", required=True, type=Path, help="Source dataset with one folder per original class.")
    parser.add_argument("--output", required=True, type=Path, help="Output dataset root.")
    parser.add_argument("--val-ratio", type=float, default=0.15)
    parser.add_argument("--test-ratio", type=float, default=0.10)
    parser.add_argument("--seed", type=int, default=42)
    return parser.parse_args()


def class_to_binary(class_name):
    name = class_name.lower()
    if "background" in name:
        return None
    if "healthy" in name:
        return "healthy"
    return "disease"


def collect_images(source):
    samples = []
    for class_dir in sorted(p for p in source.iterdir() if p.is_dir()):
        label = class_to_binary(class_dir.name)
        if label is None:
            continue
        for path in class_dir.rglob("*"):
            if path.is_file() and path.suffix.lower() in IMAGE_EXTENSIONS:
                samples.append((path, label))
    return samples


def copy_split(samples, output, val_ratio, test_ratio, seed):
    random.seed(seed)
    by_label = {"healthy": [], "disease": []}
    for path, label in samples:
        by_label[label].append(path)

    for split in ("train", "validation", "test"):
        for label in by_label:
            (output / split / label).mkdir(parents=True, exist_ok=True)

    counts = {}
    for label, paths in by_label.items():
        random.shuffle(paths)
        n_total = len(paths)
        n_test = int(n_total * test_ratio)
        n_val = int(n_total * val_ratio)

        split_paths = {
            "test": paths[:n_test],
            "validation": paths[n_test:n_test + n_val],
            "train": paths[n_test + n_val:],
        }
        counts[label] = {split: len(items) for split, items in split_paths.items()}

        for split, items in split_paths.items():
            for index, src in enumerate(items):
                dst = output / split / label / f"{label}_{index:06d}{src.suffix.lower()}"
                shutil.copy2(src, dst)
    return counts


def main():
    args = parse_args()
    samples = collect_images(args.source)
    if not samples:
        raise SystemExit(f"No images found under {args.source}")

    counts = copy_split(samples, args.output, args.val_ratio, args.test_ratio, args.seed)
    print("Done.")
    for label, split_counts in counts.items():
        print(label, split_counts)


if __name__ == "__main__":
    main()
