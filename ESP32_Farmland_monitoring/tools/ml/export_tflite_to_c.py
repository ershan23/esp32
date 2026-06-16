import argparse
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(description="Export a .tflite model to ESP-IDF C source/header files.")
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--name", default="plant_binary_model")
    return parser.parse_args()


def c_array(data):
    lines = []
    for offset in range(0, len(data), 12):
        chunk = data[offset:offset + 12]
        lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    return "\n".join(lines)


def main():
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    data = args.model.read_bytes()
    symbol = args.name
    guard = f"__{symbol.upper()}_H_"

    header = f"""#ifndef {guard}
#define {guard}

#include <stddef.h>
#include <stdint.h>

#define PLANT_MODEL_AVAILABLE 1

extern const uint8_t {symbol}[];
extern const size_t {symbol}_len;

#endif
"""

    source = f"""#include "{symbol}.h"

const uint8_t {symbol}[] __attribute__((aligned(16))) = {{
{c_array(data)}
}};

const size_t {symbol}_len = sizeof({symbol});
"""

    (args.output_dir / f"{symbol}.h").write_text(header, encoding="utf-8")
    (args.output_dir / f"{symbol}.c").write_text(source, encoding="utf-8")
    print(f"Wrote {args.output_dir / (symbol + '.h')}")
    print(f"Wrote {args.output_dir / (symbol + '.c')}")
    print(f"Bytes: {len(data)}")


if __name__ == "__main__":
    main()
