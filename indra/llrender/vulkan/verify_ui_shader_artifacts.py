#!/usr/bin/env python3
"""Regenerate UI shaders and compare the exact embedded SPIR-V artifacts."""

from pathlib import Path
import hashlib
import re
import shutil
import struct
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent
    tools = {name: shutil.which(name) for name in ("glslangValidator", "spirv-val")}
    for name, path in tools.items():
        if path is None:
            raise SystemExit(f"missing required offline tool: {name}")
    header = (root / "llrendervulkanuishaders.h").read_text()
    with tempfile.TemporaryDirectory(prefix="vulkan-ui-shaders-") as temporary:
        for stage, name in (("vert", "UI_VERTEX_SHADER"), ("frag", "UI_FRAGMENT_SHADER")):
            output = Path(temporary) / f"ui.{stage}.spv"
            subprocess.run([tools["glslangValidator"], "-V", "--target-env", "vulkan1.1", "-S", stage,
                            str(root / "shaders" / f"ui.{stage}.glsl"), "-o", str(output)], check=True)
            subprocess.run([tools["spirv-val"], "--target-env", "vulkan1.1", str(output)], check=True)
            match = re.search(rf"{name}\[\]\s*=\s*\{{(.*?)\}};", header, re.DOTALL)
            if match is None:
                raise SystemExit(f"missing embedded array {name}")
            embedded = b"".join(struct.pack("<I", int(word, 16)) for word in re.findall(r"0x([0-9a-fA-F]{8})", match[1]))
            compiled = output.read_bytes()
            if compiled != embedded:
                raise SystemExit(f"{name} differs from its source")
            print(f"{stage}: {len(compiled)} bytes SHA-256 {hashlib.sha256(compiled).hexdigest()}")


if __name__ == "__main__":
    main()
