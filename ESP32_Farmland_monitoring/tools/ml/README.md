# ESP32-S3 Plant Binary Model

This folder contains the training and conversion pipeline for a small plant leaf binary classifier:

- `healthy`
- `disease`

The target model is a fully-int8 TensorFlow Lite model for ESP32-S3.

## Dataset Layout

Preferred layout:

```text
datasets/plant_binary/
  train/
    healthy/
    disease/
  validation/
    healthy/
    disease/
  test/
    healthy/
    disease/
```

If you have a PlantVillage-style dataset with many class folders, run:

```powershell
python tools/ml/prepare_binary_dataset.py `
  --source C:\path\to\PlantVillage `
  --output datasets/plant_binary
```

Folder names containing `healthy` become `healthy`; other leaf disease folders become `disease`; `background` folders are skipped.

If you downloaded the PlantVillage GitHub ZIP and extracted `data_distribution_for_SVM`, run:

```powershell
python tools/ml/prepare_svm_distribution.py `
  --source datasets/source/PlantVillage-SVM/PlantVillage-Dataset-master/data_distribution_for_SVM `
  --output datasets/plant_binary
```

Useful public data sources:

- PlantVillage GitHub mirror: https://github.com/spMohanty/PlantVillage-Dataset
- TensorFlow Datasets `plant_village`: https://www.tensorflow.org/datasets/catalog/plant_village
- Kaggle PlantVillage: https://www.kaggle.com/datasets/mohitsingh1804/plantvillage
- Kaggle New Plant Diseases Dataset: https://www.kaggle.com/datasets/vipoooool/new-plant-diseases-dataset

## Create Python Environment

Use a separate Python environment, not the ESP-IDF venv:

```powershell
python -m venv .venv-ml
.\.venv-ml\Scripts\Activate.ps1
python -m pip install -r tools/ml/requirements.txt
```

## Train And Convert

```powershell
python tools/ml/train_binary_model.py `
  --dataset datasets/plant_binary `
  --output models/plant_binary `
  --img-size 96 `
  --epochs 25
```

Outputs:

```text
models/plant_binary/
  labels.txt
  metadata.json
  plant_binary_float.tflite
  plant_binary_int8.tflite
  plant_binary_model.keras
```

## Export C Array

```powershell
python tools/ml/export_tflite_to_c.py `
  --model models/plant_binary/plant_binary_int8.tflite `
  --output-dir components/ML/generated `
  --name plant_binary_model
```

This creates:

```text
components/ML/generated/plant_binary_model.h
components/ML/generated/plant_binary_model.c
```

## ESP32-S3 Target

Recommended model settings:

- input: `96x96x3`
- input dtype: `int8`
- output dtype: `int8`
- labels: `healthy`, `disease`
- expected model size: usually under a few hundred KB
