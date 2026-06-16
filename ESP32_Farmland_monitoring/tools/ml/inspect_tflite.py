import argparse
import math
from collections import Counter
from pathlib import Path

import tflite


TYPE_BYTES = {
    "FLOAT32": 4,
    "INT32": 4,
    "UINT8": 1,
    "INT8": 1,
    "INT64": 8,
    "BOOL": 1,
}


def enum_name(cls, value):
    for key, candidate in cls.__dict__.items():
        if key.isupper() and candidate == value:
            return key
    return str(value)


def tensor_info(subgraph, index):
    tensor = subgraph.Tensors(index)
    shape = [tensor.Shape(i) for i in range(tensor.ShapeLength())]
    type_name = enum_name(tflite.TensorType, tensor.Type())
    quant = tensor.Quantization()
    scales = [quant.Scale(i) for i in range(quant.ScaleLength())] if quant else []
    zero_points = [quant.ZeroPoint(i) for i in range(quant.ZeroPointLength())] if quant else []
    elements = math.prod(shape) if shape else 1
    bytes_est = elements * TYPE_BYTES.get(type_name, 4)
    return {
        "index": index,
        "name": tensor.Name().decode(errors="replace") if tensor.Name() else "",
        "shape": shape,
        "type": type_name,
        "bytes": bytes_est,
        "scale": scales[:8],
        "zero_point": zero_points[:8],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("model", type=Path)
    args = parser.parse_args()

    model = tflite.Model.GetRootAsModel(args.model.read_bytes(), 0)
    subgraph = model.Subgraphs(0)
    print("version:", model.Version())
    print("size:", args.model.stat().st_size)
    print("inputs:")
    for i in range(subgraph.InputsLength()):
        print(tensor_info(subgraph, subgraph.Inputs(i)))
    print("outputs:")
    for i in range(subgraph.OutputsLength()):
        print(tensor_info(subgraph, subgraph.Outputs(i)))

    op_counts = Counter()
    for i in range(subgraph.OperatorsLength()):
        op = subgraph.Operators(i)
        opcode = model.OperatorCodes(op.OpcodeIndex())
        name = enum_name(tflite.BuiltinOperator, opcode.BuiltinCode())
        op_counts[name] += 1
    print("ops:")
    for name, count in sorted(op_counts.items()):
        print(f"  {name}: {count}")

    type_counts = Counter(enum_name(tflite.TensorType, subgraph.Tensors(i).Type()) for i in range(subgraph.TensorsLength()))
    print("tensor types:", dict(type_counts))
    largest = max((tensor_info(subgraph, i) for i in range(subgraph.TensorsLength())), key=lambda item: item["bytes"])
    print("largest tensor:", largest)


if __name__ == "__main__":
    main()
