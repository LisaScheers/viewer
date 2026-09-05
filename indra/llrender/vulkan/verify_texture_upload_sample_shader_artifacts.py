#!/usr/bin/env python3
"""Regenerate and verify the embedded sampled texture-upload shaders."""

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
        "source": "textureupload.vert.glsl",
        "stage": "vert",
        "array": "TEXTURE_UPLOAD_SAMPLE_VERTEX_SHADER",
        "size": 1112,
        "sha256": "139f3d06e998cdd95ad6ae751dd97cf7ecaeb9c210efca379a7b1ee73270789c",
    },
    "fragment": {
        "source": "textureupload.frag.glsl",
        "stage": "frag",
        "array": "TEXTURE_UPLOAD_SAMPLE_FRAGMENT_SHADER",
        "size": 628,
        "sha256": "2d07ec80932a25934493be1d4f8bdfb3ca3d2bac0cf3ffa9cbb7d7520bdaafb1",
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


def reflected_interface(reflection: object, key: str) -> list[tuple[object, object]]:
    if not isinstance(reflection, dict):
        raise SystemExit("shader reflection root is not an object")
    resources = reflection.get(key, [])
    if not isinstance(resources, list):
        raise SystemExit(f"reflected {key} is not an array: {resources!r}")
    result: list[tuple[object, object]] = []
    for resource in resources:
        if not isinstance(resource, dict):
            raise SystemExit(f"reflected {key} contains a non-object: {resource!r}")
        result.append((resource.get("location"), resource.get("type")))
    return result


def descriptor_resources(value: object) -> list[tuple[object, object, object]]:
    resources: list[tuple[object, object, object]] = []
    if isinstance(value, dict):
        if "set" in value or "binding" in value:
            resources.append((value.get("set"), value.get("binding"), value.get("type")))
        for child in value.values():
            resources.extend(descriptor_resources(child))
    elif isinstance(value, list):
        for child in value:
            resources.extend(descriptor_resources(child))
    return resources


def verify_common(label: str, mode: str, disassembly: str, reflection: object) -> None:
    if not isinstance(reflection, dict):
        raise SystemExit(f"{label} reflection root is not an object")
    entry_points = reflection.get("entryPoints")
    if entry_points != [{"name": "main", "mode": mode}]:
        raise SystemExit(f"{label} shader has an unexpected entry-point contract: {entry_points!r}")
    if re.search(r"\bPushConstant\b", disassembly) or reflection.get("push_constants"):
        raise SystemExit(f"{label} shader unexpectedly declares a push constant")


def verify_vertex(disassembly: str, reflection: object) -> None:
    verify_common("vertex", "vert", disassembly, reflection)
    if re.search(r"\bBuiltIn\s+VertexIndex\b", disassembly):
        raise SystemExit("vertex shader unexpectedly uses BuiltIn VertexIndex")
    if reflected_interface(reflection, "inputs") != [(0, "vec3")]:
        raise SystemExit(f"vertex shader input is not exactly location-zero vec3: {reflection!r}")
    if reflected_interface(reflection, "outputs") != [(0, "vec2")]:
        raise SystemExit(f"vertex shader output is not exactly location-zero vec2: {reflection!r}")
    if descriptor_resources(reflection):
        raise SystemExit("vertex shader unexpectedly declares a descriptor")


def verify_fragment(disassembly: str, reflection: object) -> None:
    verify_common("fragment", "frag", disassembly, reflection)
    if reflected_interface(reflection, "inputs") != [(0, "vec2")]:
        raise SystemExit(f"fragment shader input is not exactly location-zero vec2: {reflection!r}")
    if reflected_interface(reflection, "outputs") != [(0, "vec4")]:
        raise SystemExit(f"fragment shader output is not exactly location-zero vec4: {reflection!r}")
    if descriptor_resources(reflection) != [(0, 0, "sampler2D")]:
        raise SystemExit(f"fragment descriptors are not exactly set-zero binding-zero sampler2D: {reflection!r}")
    sample_operations = re.findall(r"\bOpImageSample\w*\b", disassembly)
    if sample_operations != ["OpImageSampleImplicitLod"]:
        raise SystemExit(f"fragment shader sampling operation changed: {sample_operations!r}")


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
    cpp_source = (directory / "llrendervulkantextureuploadsamplepipeline.cpp").read_text(encoding="utf-8")

    with tempfile.TemporaryDirectory(prefix="texture-upload-sample-shaders-") as temporary:
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
                    f"-I{directory / 'shaders'}",
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

            if label == "vertex":
                verify_vertex(disassembly, reflection)
            else:
                verify_fragment(disassembly, reflection)

            generated = output.read_bytes()
            embedded = embedded_bytes(cpp_source, str(contract["array"]))
            digest = hashlib.sha256(generated).hexdigest()
            if len(generated) != contract["size"] or digest != contract["sha256"]:
                raise SystemExit(f"{label} compiler output changed: {len(generated)} bytes, SHA-256 {digest}")
            if generated != embedded:
                raise SystemExit(f"{label} compiler output differs from the embedded word array")
            print(f"{label}: {len(generated)} bytes, SHA-256 {digest}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
