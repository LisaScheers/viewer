#!/usr/bin/env python3
"""Regenerate and verify the embedded presentation shaders."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path


SHADERS = {
    "vertex": {
        "source": "presentation.vert.glsl",
        "stage": "vert",
        "array": "PRESENTATION_VERTEX_SHADER",
        "size": 636,
        "sha256": "a2eafa25a5e418187e60e41a8a19a958f9394d83a03506e5d17ec285ba8a7b3f",
    },
    "fragment": {
        "source": "presentation.frag.glsl",
        "stage": "frag",
        "array": "PRESENTATION_FRAGMENT_SHADER",
        "size": 304,
        "sha256": "784c8df9710a1b546fa9873249b463567449f4e48b37c656c6d2ac3584e897cc",
    },
}


def require_tool(explicit: str | None, name: str) -> str:
    tool = explicit or shutil.which(name)
    if not tool:
        raise SystemExit(f"missing required offline tool: {name}")
    return tool


def embedded_bytes(source: str, array_name: str) -> bytes:
    match = re.search(
        rf"constexpr\s+std::array<std::uint32_t,\s*\d+>\s+{array_name}\s*\{{(.*?)\n\s*\}};",
        source,
        re.DOTALL,
    )
    if not match:
        raise SystemExit(f"cannot find embedded array {array_name}")
    words = [int(word, 16) for word in re.findall(r"0x([0-9a-fA-F]{8})", match.group(1))]
    return b"".join(struct.pack("<I", word) for word in words)


def run(command: list[str]) -> None:
    subprocess.run(command, check=True)


def capture(command: list[str]) -> str:
    return subprocess.run(command, check=True, stdout=subprocess.PIPE, text=True).stdout


def has_descriptor_resource(value: object) -> bool:
    if isinstance(value, dict):
        if "set" in value and "binding" in value:
            return True
        return any(has_descriptor_resource(child) for child in value.values())
    if isinstance(value, list):
        return any(has_descriptor_resource(child) for child in value)
    return False


def verify_no_descriptors(label: str, disassembly: str, reflection: object) -> None:
    if re.search(r"\b(?:DescriptorSet|Binding)\b", disassembly) or has_descriptor_resource(reflection):
        raise SystemExit(f"{label} shader unexpectedly declares a descriptor")


def verify_vertex_interface(disassembly: str, reflection: object) -> None:
    if re.search(r"\bBuiltIn\s+VertexIndex\b", disassembly):
        raise SystemExit("vertex shader unexpectedly uses BuiltIn VertexIndex")
    if not isinstance(reflection, dict):
        raise SystemExit("vertex reflection root is not an object")

    entry_points = reflection.get("entryPoints")
    if entry_points != [{"name": "main", "mode": "vert"}]:
        raise SystemExit(f"vertex shader has an unexpected entry-point contract: {entry_points!r}")

    inputs = reflection.get("inputs")
    if not isinstance(inputs, list) or len(inputs) != 1:
        raise SystemExit(f"vertex shader must declare exactly one reflected input: {inputs!r}")
    vertex_input = inputs[0]
    if not isinstance(vertex_input, dict) or vertex_input.get("location") != 0 or vertex_input.get("type") != "vec3":
        raise SystemExit(f"vertex shader input must be vec3 at location zero: {vertex_input!r}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glslang-validator")
    parser.add_argument("--spirv-val")
    parser.add_argument("--spirv-dis")
    parser.add_argument("--spirv-cross")
    args = parser.parse_args()

    glslang = require_tool(args.glslang_validator, "glslangValidator")
    validator = require_tool(args.spirv_val, "spirv-val")
    disassembler = require_tool(args.spirv_dis, "spirv-dis")
    reflector = require_tool(args.spirv_cross, "spirv-cross")
    directory = Path(__file__).resolve().parent
    cpp_source = (directory / "llrendervulkanswapchainpresentationpipeline.cpp").read_text(encoding="utf-8")

    with tempfile.TemporaryDirectory(prefix="presentation-shaders-") as temporary:
        output_directory = Path(temporary)
        for label, contract in SHADERS.items():
            output = output_directory / f"{label}.spv"
            run(
                [
                    glslang,
                    "-V",
                    "--target-env",
                    "vulkan1.1",
                    "-S",
                    str(contract["stage"]),
                    "-Os",
                    "-g0",
                    "-o",
                    str(output),
                    str(directory / "shaders" / str(contract["source"])),
                ]
            )
            run([validator, "--target-env", "vulkan1.1", str(output)])
            disassembly = capture([disassembler, str(output)])
            try:
                reflection = json.loads(capture([reflector, str(output), "--reflect"]))
            except json.JSONDecodeError as error:
                raise SystemExit(f"{label} reflection is not valid JSON: {error}") from error
            verify_no_descriptors(label, disassembly, reflection)
            if label == "vertex":
                verify_vertex_interface(disassembly, reflection)
            generated = output.read_bytes()
            embedded = embedded_bytes(cpp_source, str(contract["array"]))
            digest = hashlib.sha256(generated).hexdigest()
            if len(generated) != contract["size"] or digest != contract["sha256"]:
                raise SystemExit(
                    f"{label} compiler output changed: {len(generated)} bytes, SHA-256 {digest}"
                )
            if generated != embedded:
                raise SystemExit(f"{label} compiler output differs from the embedded word array")
            print(f"{label}: {len(generated)} bytes, SHA-256 {digest}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
