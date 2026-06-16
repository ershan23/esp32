import argparse
import random
import shutil
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(description="Convert PlantVillage data_distribution_for_SVM into healthy/disease splits.")
    parser.add_argument("--source", required=True, type=Path, help="Path to data_distribution_for_SVM")
    parser.add_argument("--output", required=True, type=Path, help="Output dataset root")
    parser.add_argument("--val-ratio", type=float, default=0.15)
    parser.add_argument("--seed", type=int, default=42)
    return parser.parse_args()


def label_from_original_path(original_path):
    class_name = Path(original_path).parent.name.lower()
    if "background" in class_name:
        return None
    if "healthy" in class_name:
        return "healthy"
    return "disease"


def read_mapping(source, split):
    mapping_path = source / f"{split}_mapping.txt"
    if not mapping_path.exists():
        raise SystemExit(f"Missing mapping file: {mapping_path}")

    samples = []
    for line in mapping_path.read_text(encoding="utf-8", errors="ignore").splitlines():
        if not line.strip():
            continue
        original_path, svm_path = line.split("\t")
        label = label_from_original_path(original_path)
        if label is None:
            continue
        parts = Path(svm_path).parts
        # Mapping uses SVM/train/35/file.JPG; extracted data uses train/35/file.JPG.
        local_rel = Path(*parts[1:]) if parts and parts[0].lower() == "svm" else Path(svm_path)
        local_path = source / local_rel
        if local_path.exists():
            samples.append((local_path, label))
    return samples


def copy_samples(samples, output, split):
    counts = {"healthy": 0, "disease": 0}
    for src, label in samples:
        dst_dir = output / split / label
        dst_dir.mkdir(parents=True, exist_ok=True)
        dst = dst_dir / f"{label}_{counts[label]:06d}{src.suffix.lower()}"
        shutil.copy2(src, dst)
        counts[label] += 1
    return counts


def main():
    args = parse_args()
    if args.output.exists():
        shutil.rmtree(args.output)

    train_samples = read_mapping(args.source, "train")
    test_samples = read_mapping(args.source, "test")
    random.seed(args.seed)
    random.shuffle(train_samples)

    val_count = int(len(train_samples) * args.val_ratio)
    validation_samples = train_samples[:val_count]
    train_samples = train_samples[val_count:]

    counts = {
        "train": copy_samples(train_samples, args.output, "train"),
        "validation": copy_samples(validation_samples, args.output, "validation"),
        "test": copy_samples(test_samples, args.output, "test"),
    }

    print("Done.")
    for split, split_counts in counts.items():
        print(split, split_counts)


if __name__ == "__main__":
    main()
