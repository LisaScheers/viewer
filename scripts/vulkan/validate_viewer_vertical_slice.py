#!/usr/bin/env python3
"""\
@file validate_viewer_vertical_slice.py
@brief Exercise the Linux Vulkan slice through the real viewer executable.

$LicenseInfo:firstyear=2026&license=viewerlgpl$
Copyright (c) 2026, Linden Research, Inc.
$/LicenseInfo$

This driver launches an unstripped secondlife-bin against a packaged viewer
tree, performs real X11 window operations, and validates the viewer's own
Vulkan lifecycle evidence. It never logs in and needs no account credentials.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import random
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from typing import Mapping, Sequence


DEFAULT_STARTUP_TIMEOUT_SECONDS = 90.0
DEFAULT_SHUTDOWN_TIMEOUT_SECONDS = 30.0
COMMAND_TIMEOUT_SECONDS = 10.0
MIN_PRESENTED_FRAMES = 10
MIN_SWAPCHAIN_REBUILDS = 3
MIN_SUSPENDS = 1


class SmokeFailure(RuntimeError):
    """A failed admission check with an actionable operator message."""


@dataclass(frozen=True)
class ImageFacts:
    width: int
    height: int
    colors: int


@dataclass(frozen=True)
class Tooling:
    xvfb: str
    openbox: str
    xdotool: str
    screenshot: str
    identify: str


@dataclass(frozen=True)
class Artifacts:
    root: Path
    state: Path
    first_image: Path
    resized_image: Path
    viewer_stdout: Path
    window_manager_log: Path
    xvfb_log: Path


def positive_float(value: str) -> float:
    parsed = float(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def window_size(value: str) -> tuple[int, int]:
    match = re.fullmatch(r"([1-9][0-9]*)x([1-9][0-9]*)", value)
    if not match:
        raise argparse.ArgumentTypeError("must have the form WIDTHxHEIGHT")
    return int(match.group(1)), int(match.group(2))


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate the real Linux secondlife-bin Vulkan vertical slice under "
            "Khronos validation. Requires Xvfb, openbox, xdotool, and ImageMagick."
        )
    )
    parser.add_argument("--viewer", type=Path, required=True, help="unstripped secondlife-bin")
    parser.add_argument("--app-dir", type=Path, required=True, help="packaged viewer application directory")
    parser.add_argument("--vulkan-icd", type=Path, required=True, help="Vulkan ICD JSON file")
    parser.add_argument(
        "--vulkan-layer-dir",
        type=Path,
        required=True,
        help="directory containing the validation layer manifest",
    )
    parser.add_argument(
        "--vulkan-loader-dir",
        type=Path,
        help="optional directory containing a Vulkan loader new enough for the required WSI extensions",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="retain logs and PNGs here; a temporary directory is removed after success",
    )
    parser.add_argument("--display", help="use this existing X11 display instead of starting isolated Xvfb and openbox")
    parser.add_argument(
        "--viewer-arg",
        action="append",
        default=[],
        help="append an extra viewer argument; may be repeated",
    )
    parser.add_argument("--initial-size", type=window_size, default=(900, 600), metavar="WIDTHxHEIGHT")
    parser.add_argument("--resized-size", type=window_size, default=(1120, 720), metavar="WIDTHxHEIGHT")
    parser.add_argument("--startup-timeout", type=positive_float, default=DEFAULT_STARTUP_TIMEOUT_SECONDS)
    parser.add_argument("--shutdown-timeout", type=positive_float, default=DEFAULT_SHUTDOWN_TIMEOUT_SECONDS)
    parser.add_argument("--settle-seconds", type=positive_float, default=2.0)
    parser.add_argument("--minimize-seconds", type=positive_float, default=1.5)
    parser.add_argument("--xvfb", default="Xvfb", help="Xvfb executable override")
    parser.add_argument("--openbox", default="openbox", help="openbox executable override")
    parser.add_argument("--xdotool", default="xdotool", help="xdotool executable override")
    parser.add_argument("--screenshot-tool", default="import", help="ImageMagick import executable override")
    parser.add_argument("--identify", default="identify", help="ImageMagick identify executable override")
    return parser.parse_args(argv)


def resolved_executable(value: str, label: str) -> str:
    candidate = Path(value).expanduser()
    if candidate.parent != Path(".") or candidate.is_absolute():
        resolved = candidate.resolve()
        if resolved.is_file() and os.access(resolved, os.X_OK):
            return str(resolved)
    else:
        found = shutil.which(value)
        if found:
            return found
    raise SmokeFailure(f"missing {label} executable: {value}")


def validate_inputs(args: argparse.Namespace) -> tuple[Path, Path, Path, Path, Path | None, Tooling]:
    viewer = args.viewer.expanduser().resolve()
    app_dir = args.app_dir.expanduser().resolve()
    vulkan_icd = args.vulkan_icd.expanduser().resolve()
    layer_dir = args.vulkan_layer_dir.expanduser().resolve()
    loader_dir = args.vulkan_loader_dir.expanduser().resolve() if args.vulkan_loader_dir else None

    if not viewer.is_file() or not os.access(viewer, os.X_OK):
        raise SmokeFailure(f"viewer is not an executable file: {viewer}")
    if not app_dir.is_dir():
        raise SmokeFailure(f"packaged application directory does not exist: {app_dir}")
    if not (app_dir / "lib").is_dir() or not (app_dir / "app_settings").is_dir():
        raise SmokeFailure(f"app directory lacks packaged lib/ or app_settings/: {app_dir}")
    if not vulkan_icd.is_file():
        raise SmokeFailure(f"Vulkan ICD manifest does not exist: {vulkan_icd}")
    if not layer_dir.is_dir() or not any(layer_dir.glob("*.json")):
        raise SmokeFailure(f"Vulkan layer directory has no JSON manifests: {layer_dir}")
    if loader_dir and not (loader_dir / "libvulkan.so.1").is_file():
        raise SmokeFailure(f"Vulkan loader directory has no libvulkan.so.1: {loader_dir}")
    if args.initial_size == args.resized_size:
        raise SmokeFailure("initial and resized window extents must differ")

    tooling = Tooling(
        xvfb=args.xvfb if args.display else resolved_executable(args.xvfb, "Xvfb"),
        openbox=args.openbox if args.display else resolved_executable(args.openbox, "openbox"),
        xdotool=resolved_executable(args.xdotool, "xdotool"),
        screenshot=resolved_executable(args.screenshot_tool, "ImageMagick import"),
        identify=resolved_executable(args.identify, "ImageMagick identify"),
    )
    return viewer, app_dir, vulkan_icd, layer_dir, loader_dir, tooling


def prepare_artifacts(output_dir: Path | None) -> tuple[Artifacts, bool]:
    temporary = output_dir is None
    if temporary:
        root = Path(tempfile.mkdtemp(prefix="secondlife-vulkan-smoke-"))
    else:
        root = output_dir.expanduser().resolve()
        root.mkdir(parents=True, exist_ok=True)

    paths = Artifacts(
        root=root,
        state=root / "state",
        first_image=root / "initial.png",
        resized_image=root / "resized.png",
        viewer_stdout=root / "secondlife-bin.output.log",
        window_manager_log=root / "openbox.log",
        xvfb_log=root / "xvfb.log",
    )
    collisions = [
        path
        for path in (
            paths.first_image,
            paths.resized_image,
            paths.viewer_stdout,
            paths.window_manager_log,
            paths.xvfb_log,
        )
        if path.exists()
    ]
    if collisions:
        raise SmokeFailure(f"output artifacts already exist: {', '.join(map(str, collisions))}")
    paths.state.mkdir(parents=True, exist_ok=False)
    return paths, temporary


def run_command(
    command: Sequence[str],
    *,
    env: Mapping[str, str],
    timeout: float = COMMAND_TIMEOUT_SECONDS,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    try:
        completed = subprocess.run(
            list(command),
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise SmokeFailure(f"command timed out after {timeout:g}s: {' '.join(command)}") from error
    if check and completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip() or "no diagnostic output"
        raise SmokeFailure(f"command failed ({completed.returncode}): {' '.join(command)}: {detail}")
    return completed


def terminate_process(process: subprocess.Popen[bytes] | None) -> None:
    if process is None or process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=5)
    except (ProcessLookupError, subprocess.TimeoutExpired):
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass


def pick_display() -> str:
    candidates = list(range(90, 200))
    random.SystemRandom().shuffle(candidates)
    for number in candidates:
        if not Path(f"/tmp/.X11-unix/X{number}").exists() and not Path(f"/tmp/.X{number}-lock").exists():
            return f":{number}"
    raise SmokeFailure("could not allocate an unused X11 display in :90..:199")


def wait_for_display(tooling: Tooling, env: Mapping[str, str], xvfb: subprocess.Popen[bytes], timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if xvfb.poll() is not None:
            raise SmokeFailure(f"Xvfb exited before its display became ready (exit {xvfb.returncode})")
        probe = run_command([tooling.xdotool, "getmouselocation"], env=env, timeout=2, check=False)
        if probe.returncode == 0:
            return
        time.sleep(0.1)
    raise SmokeFailure(f"X11 display {env['DISPLAY']} did not become ready")


def wait_for_window(
    tooling: Tooling,
    env: Mapping[str, str],
    viewer: subprocess.Popen[bytes],
    timeout: float,
) -> str:
    deadline = time.monotonic() + timeout
    last_ids: list[str] = []
    while time.monotonic() < deadline:
        if viewer.poll() is not None:
            raise SmokeFailure(f"secondlife-bin exited before its window appeared (exit {viewer.returncode})")
        result = run_command(
            [tooling.xdotool, "search", "--onlyvisible", "--name", "^Second Life( |$)"],
            env=env,
            timeout=2,
            check=False,
        )
        last_ids = list(dict.fromkeys(result.stdout.split())) if result.returncode == 0 else []
        if len(last_ids) == 1:
            return last_ids[0]
        if len(last_ids) > 1:
            raise SmokeFailure(f"expected one visible Second Life window, found {len(last_ids)}: {', '.join(last_ids)}")
        time.sleep(0.2)
    raise SmokeFailure(f"no visible Second Life window appeared within {timeout:g}s (last matches: {last_ids})")


def ensure_viewer_alive(viewer: subprocess.Popen[bytes], operation: str) -> None:
    returncode = viewer.poll()
    if returncode is not None:
        raise SmokeFailure(f"secondlife-bin exited during {operation} (exit {returncode})")


def screenshot(tooling: Tooling, env: Mapping[str, str], window_id: str, destination: Path) -> None:
    run_command([tooling.screenshot, "-window", window_id, str(destination)], env=env)
    if not destination.is_file() or destination.stat().st_size == 0:
        raise SmokeFailure(f"ImageMagick did not create a screenshot: {destination}")


def image_facts(tooling: Tooling, env: Mapping[str, str], image: Path) -> ImageFacts:
    result = run_command(
        [tooling.identify, "-format", "%w %h %k", str(image)],
        env=env,
    )
    fields = result.stdout.strip().split()
    if len(fields) != 3:
        raise SmokeFailure(f"unexpected ImageMagick identify output for {image}: {result.stdout!r}")
    try:
        facts = ImageFacts(*(int(value) for value in fields))
    except ValueError as error:
        raise SmokeFailure(f"non-numeric ImageMagick identify output for {image}: {result.stdout!r}") from error
    if facts.width <= 0 or facts.height <= 0 or facts.colors < 8:
        raise SmokeFailure(
            f"screenshot is blank or insufficiently varied: {image} "
            f"({facts.width}x{facts.height}, {facts.colors} colors)"
        )
    return facts


def key_values(line: str) -> dict[str, str]:
    return dict(re.findall(r"\b([a-z_]+)=([^\s]+)", line))


def integer_field(fields: Mapping[str, str], name: str, context: str) -> int:
    try:
        return int(fields[name])
    except KeyError as error:
        raise SmokeFailure(f"missing {name}= in {context}") from error
    except ValueError as error:
        raise SmokeFailure(f"non-integer {name}={fields[name]} in {context}") from error


def require_zero(fields: Mapping[str, str], name: str, context: str) -> None:
    value = fields.get(name)
    if value not in {"0", "false"}:
        raise SmokeFailure(f"expected {name}=0 in {context}, got {value!r}")


def validate_viewer_log(log_path: Path) -> dict[str, int]:
    if not log_path.is_file():
        raise SmokeFailure(f"viewer log was not created: {log_path}")
    text = log_path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()

    owner_indexes = [
        index
        for index, line in enumerate(lines)
        if "window_owner=viewer" in line and "callbacks=viewer" in line
    ]
    if len(owner_indexes) != 1:
        raise SmokeFailure(f"expected one viewer window-owner record, found {len(owner_indexes)}")
    owner_fields = key_values(lines[owner_indexes[0]])
    if integer_field(owner_fields, "live_windows", "window-owner record") != 1:
        raise SmokeFailure("viewer did not own exactly one live window")

    status_records = [
        (index, line, key_values(line))
        for index, line in enumerate(lines)
        if "status=" in line and "VulkanViewerSlice" in line
    ]
    if not status_records:
        raise SmokeFailure("viewer emitted no VulkanViewerSlice status record")
    status_index, status_line, status = status_records[-1]
    if status.get("status") != "stopped":
        raise SmokeFailure(f"final Vulkan viewer status was not stopped: {status_line.strip()}")

    frames = integer_field(status, "frames", "final status")
    rebuilds = integer_field(status, "rebuilds", "final status")
    suspends = integer_field(status, "suspends", "final status")
    if frames <= MIN_PRESENTED_FRAMES:
        raise SmokeFailure(f"too few presented frames: {frames} (need > {MIN_PRESENTED_FRAMES})")
    if rebuilds < MIN_SWAPCHAIN_REBUILDS:
        raise SmokeFailure(
            f"resize/restore did not rebuild enough swapchains: "
            f"{rebuilds} (need >= {MIN_SWAPCHAIN_REBUILDS})"
        )
    if suspends < MIN_SUSPENDS:
        raise SmokeFailure(f"minimize did not produce a suspended zero-extent transition: {suspends}")
    for field in (
        "validation_messages",
        "gl_context",
        "gl_manager",
        "gl_context_create_attempts",
        "gl_swap_attempts",
    ):
        require_zero(status, field, "final status")
    if status.get("gl_audit_armed") not in {"1", "true"}:
        raise SmokeFailure("process-wide SDL OpenGL auditing was not armed")
    if integer_field(status, "live_windows", "final status") != 1:
        raise SmokeFailure("viewer window was not live while the Vulkan runtime stopped")

    teardown_records = [
        (index, line, key_values(line))
        for index, line in enumerate(lines)
        if "window_retired=1" in line and "VulkanViewerSlice" in line
    ]
    if len(teardown_records) != 1:
        raise SmokeFailure(f"expected one orderly window-retirement record, found {len(teardown_records)}")
    teardown_index, _, teardown = teardown_records[0]
    if teardown_index <= status_index:
        raise SmokeFailure("window retirement was logged before Vulkan runtime shutdown")
    if integer_field(teardown, "live_windows", "window-retirement record") != 0:
        raise SmokeFailure("native window remained live after viewer teardown")
    for field in ("gl_context_create_attempts", "gl_swap_attempts", "gl_manager", "shader_manager"):
        require_zero(teardown, field, "window-retirement record")
    if teardown.get("gl_audit_armed") not in {"1", "true"}:
        raise SmokeFailure("SDL OpenGL auditing was not retained through teardown")

    failures = [line.strip() for line in lines if "VulkanViewerSlice" in line and "failure=" in line]
    if failures:
        raise SmokeFailure(f"viewer reported a Vulkan slice failure: {failures[-1]}")
    return {"frames": frames, "rebuilds": rebuilds, "suspends": suspends}


def diagnostic_tail(path: Path, lines: int = 20) -> str:
    if not path.is_file():
        return ""
    content = path.read_text(encoding="utf-8", errors="replace").splitlines()
    return "\n".join(content[-lines:])


def execute(args: argparse.Namespace, artifacts: Artifacts) -> tuple[ImageFacts, ImageFacts, dict[str, int]]:
    viewer_path, app_dir, vulkan_icd, layer_dir, loader_dir, tooling = validate_inputs(args)
    runtime_dir = artifacts.state / "runtime"
    user_dir = artifacts.state / "user"
    runtime_dir.mkdir(parents=True)
    runtime_dir.chmod(0o700)
    user_dir.mkdir(parents=True)

    environment = os.environ.copy()
    library_dirs = [str(app_dir / "lib")]
    if loader_dir:
        library_dirs.insert(0, str(loader_dir))
    if environment.get("LD_LIBRARY_PATH"):
        library_dirs.append(environment["LD_LIBRARY_PATH"])
    library_path = os.pathsep.join(library_dirs)
    environment.update(
        {
            "LD_LIBRARY_PATH": library_path,
            "SECONDLIFE_USER_DIR": str(user_dir),
            "VK_ICD_FILENAMES": str(vulkan_icd),
            "VK_LAYER_PATH": str(layer_dir),
            "SDL_VIDEODRIVER": "x11",
            "XDG_RUNTIME_DIR": str(runtime_dir),
            "XDG_CACHE_HOME": str(artifacts.state / "xdg-cache"),
            "XDG_CONFIG_HOME": str(artifacts.state / "xdg-config"),
            "XDG_DATA_HOME": str(artifacts.state / "xdg-data"),
        }
    )

    xvfb: subprocess.Popen[bytes] | None = None
    openbox: subprocess.Popen[bytes] | None = None
    viewer: subprocess.Popen[bytes] | None = None
    opened_files = []
    try:
        if args.display:
            environment["DISPLAY"] = args.display
            probe = run_command([tooling.xdotool, "getmouselocation"], env=environment, timeout=5, check=False)
            if probe.returncode != 0:
                raise SmokeFailure(f"explicit X11 display is unavailable: {args.display}: {probe.stderr.strip()}")
        else:
            environment["DISPLAY"] = pick_display()
            xvfb_output = artifacts.xvfb_log.open("wb")
            opened_files.append(xvfb_output)
            xvfb = subprocess.Popen(
                [tooling.xvfb, environment["DISPLAY"], "-screen", "0", "1600x1000x24", "-nolisten", "tcp", "-noreset"],
                env=environment,
                stdout=xvfb_output,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            wait_for_display(tooling, environment, xvfb, timeout=10)
            openbox_output = artifacts.window_manager_log.open("wb")
            opened_files.append(openbox_output)
            openbox = subprocess.Popen(
                [tooling.openbox, "--sm-disable"],
                env=environment,
                stdout=openbox_output,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            time.sleep(0.5)
            if openbox.poll() is not None:
                raise SmokeFailure(f"openbox exited during startup (exit {openbox.returncode})")

        viewer_output = artifacts.viewer_stdout.open("wb")
        opened_files.append(viewer_output)
        command = [
            str(viewer_path),
            "--vulkan",
            "--multiple",
            "--skipupdatecheck",
            "--noaudio",
            "--novoice",
            "--nonotifications",
            "--noninteractive",
            "--disablecrashlogger",
            *args.viewer_arg,
        ]
        viewer = subprocess.Popen(
            command,
            cwd=app_dir,
            env=environment,
            stdout=viewer_output,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        window_id = wait_for_window(tooling, environment, viewer, args.startup_timeout)

        initial_width, initial_height = args.initial_size
        run_command(
            [tooling.xdotool, "windowsize", "--sync", window_id, str(initial_width), str(initial_height)],
            env=environment,
        )
        run_command([tooling.xdotool, "windowactivate", "--sync", window_id], env=environment)
        run_command(
            [tooling.xdotool, "mousemove", "--window", window_id, "120", "120", "click", "1"],
            env=environment,
        )
        run_command([tooling.xdotool, "key", "--clearmodifiers", "--window", window_id, "F15"], env=environment)
        time.sleep(args.settle_seconds)
        ensure_viewer_alive(viewer, "input callback exercise")
        screenshot(tooling, environment, window_id, artifacts.first_image)

        resized_width, resized_height = args.resized_size
        run_command(
            [tooling.xdotool, "windowsize", "--sync", window_id, str(resized_width), str(resized_height)],
            env=environment,
        )
        time.sleep(args.settle_seconds)
        ensure_viewer_alive(viewer, "resize")
        screenshot(tooling, environment, window_id, artifacts.resized_image)

        run_command([tooling.xdotool, "windowminimize", "--sync", window_id], env=environment)
        time.sleep(args.minimize_seconds)
        ensure_viewer_alive(viewer, "zero-extent minimize")
        run_command([tooling.xdotool, "windowmap", "--sync", window_id], env=environment)
        run_command([tooling.xdotool, "windowactivate", "--sync", window_id], env=environment)
        time.sleep(args.settle_seconds)
        ensure_viewer_alive(viewer, "restore and resumed frame progress")

        # Ask the window manager to close the client through its normal key
        # binding. xdotool's windowclose can destroy the X11 window before SDL
        # drains focus events, which makes that tool action a teardown race
        # rather than a viewer shutdown test.
        run_command([tooling.xdotool, "windowactivate", "--sync", window_id], env=environment)
        run_command([tooling.xdotool, "key", "--clearmodifiers", "Alt+F4"], env=environment)
        try:
            returncode = viewer.wait(timeout=args.shutdown_timeout)
        except subprocess.TimeoutExpired as error:
            raise SmokeFailure(f"viewer did not exit after WM-close within {args.shutdown_timeout:g}s") from error
        if returncode != 0:
            raise SmokeFailure(f"secondlife-bin returned {returncode} after WM-close")

        first = image_facts(tooling, environment, artifacts.first_image)
        resized = image_facts(tooling, environment, artifacts.resized_image)
        if (first.width, first.height) == (resized.width, resized.height):
            raise SmokeFailure(
                f"screenshots did not prove a drawable resize: both are {first.width}x{first.height}"
            )
        evidence = validate_viewer_log(user_dir / "logs" / "SecondLife.log")
        return first, resized, evidence
    finally:
        terminate_process(viewer)
        terminate_process(openbox)
        terminate_process(xvfb)
        for opened_file in opened_files:
            opened_file.close()


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    artifacts: Artifacts | None = None
    temporary = False
    try:
        artifacts, temporary = prepare_artifacts(args.output_dir)
        first, resized, evidence = execute(args, artifacts)
    except (SmokeFailure, OSError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        if artifacts:
            print(f"artifacts retained: {artifacts.root}", file=sys.stderr)
            viewer_log = artifacts.state / "user" / "logs" / "SecondLife.log"
            tail = diagnostic_tail(viewer_log) or diagnostic_tail(artifacts.viewer_stdout)
            if tail:
                print("diagnostic tail:", file=sys.stderr)
                print(tail, file=sys.stderr)
        return 1

    retained = not temporary
    print(
        "PASS: "
        f"frames={evidence['frames']} rebuilds={evidence['rebuilds']} suspends={evidence['suspends']} "
        f"images={first.width}x{first.height},{resized.width}x{resized.height} "
        f"screenshots={artifacts.first_image},{artifacts.resized_image} "
        f"log={artifacts.state / 'user' / 'logs' / 'SecondLife.log'} "
        f"artifacts={artifacts.root}{'' if retained else ' (temporary; removing)'}"
    )
    if temporary:
        shutil.rmtree(artifacts.root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
