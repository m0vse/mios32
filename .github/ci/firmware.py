#!/usr/bin/env python3
"""Discover, select, and build MIOS32 firmware applications for CI."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Iterable

from release import next_version


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = Path(__file__).with_name("firmware-config.json")
COMMON_MK_RE = re.compile(
    r"include\s+\$\(MIOS32_PATH\)/include/makefile/common\.mk"
)
SHARED_PREFIXES = (
    "bin/",
    "bootloader/",
    "drivers/",
    "etc/",
    "FreeRTOS/",
    "include/",
    "mios32/",
    "modules/",
    "programming_models/",
)
SOURCE_ENV_FILES = {
    "source_me_BLUE_PILL",
    "source_me_MBHP_CORE_LPC17",
    "source_me_MBHP_CORE_STM32",
    "source_me_MBHP_CORE_STM32F4",
}
CI_FORCE_ALL_PREFIXES = (
    ".github/ci/firmware",
    ".github/workflows/firmware.yml",
)


def load_config() -> dict:
    config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    release_platforms = set(config["release_platforms"])
    known_platforms = set(config["platforms"])
    if release_platforms != known_platforms:
        raise ValueError("release_platforms must contain every configured platform")
    for app in config["release_apps"]:
        supported = set(
            config.get("platform_overrides", {}).get(app["path"], known_platforms)
        )
        if supported != release_platforms:
            raise ValueError(
                f"Release application {app['id']} must build for every release platform"
            )
    return config


def app_id(path: str) -> str:
    return path.removeprefix("apps/").replace("/", "-").replace(" ", "_")


def discover_apps(config: dict) -> list[dict]:
    excluded = config.get("excluded_apps", {})
    overrides = config.get("platform_overrides", {})
    release_ids = {app["path"]: app["id"] for app in config["release_apps"]}
    all_platforms = list(config["platforms"])
    apps = []
    for makefile in sorted((ROOT / "apps").rglob("Makefile")):
        relative_dir = makefile.parent.relative_to(ROOT).as_posix()
        if relative_dir in excluded:
            continue
        content = makefile.read_text(encoding="utf-8", errors="replace")
        if not COMMON_MK_RE.search(content):
            continue
        apps.append(
            {
                "id": release_ids.get(relative_dir, app_id(relative_dir)),
                "path": relative_dir,
                "platforms": overrides.get(relative_dir, all_platforms),
            }
        )
    return apps


def git_changed_files(base: str, head: str) -> list[str]:
    if not base or set(base) == {"0"}:
        return []
    result = subprocess.run(
        ["git", "diff", "--name-only", base, head],
        cwd=ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode:
        print(result.stderr, file=sys.stderr)
        return []
    return [line.strip().replace("\\", "/") for line in result.stdout.splitlines() if line.strip()]


def is_shared_change(path: str) -> bool:
    return path in SOURCE_ENV_FILES or path.startswith(SHARED_PREFIXES)


def select_apps(
    config: dict, apps: list[dict], changed: Iterable[str], force_all: bool
) -> tuple[list[dict], list[dict], str]:
    changed = list(changed)
    ci_force_all = any(path.startswith(CI_FORCE_ALL_PREFIXES) for path in changed)
    shared = any(is_shared_change(path) for path in changed)

    if force_all or ci_force_all or shared or not changed:
        selected = apps
        reason = "manual" if force_all else "CI definition" if ci_force_all else "shared core" if shared else "initial revision"
    else:
        selected = [
            app
            for app in apps
            if any(path == app["path"] or path.startswith(app["path"] + "/") for path in changed)
        ]
        reason = "application paths"

    selected_paths = {app["path"] for app in selected}
    release_apps = []
    if force_all or shared:
        release_apps = config["release_apps"]
    elif not ci_force_all:
        release_apps = [app for app in config["release_apps"] if app["path"] in selected_paths]

    return selected, release_apps, reason


def write_github_output(values: dict[str, str]) -> None:
    output_path = os.environ.get("GITHUB_OUTPUT")
    if not output_path:
        print(json.dumps(values, indent=2))
        return
    with open(output_path, "a", encoding="utf-8") as output:
        for key, value in values.items():
            output.write(f"{key}={value}\n")


def command_select(args: argparse.Namespace) -> int:
    config = load_config()
    apps = discover_apps(config)
    changed = git_changed_files(args.base, args.head)
    selected, release_apps, reason = select_apps(config, apps, changed, args.all)
    version_by_path = {}
    versioned_releases = []
    for app in release_apps:
        version = next_version(app["id"], app["base_version"], args.head)
        version_by_path[app["path"]] = version
        versioned_releases.append({**app, "version": version})
    selected = [
        {**app, "release_version": version_by_path[app["path"]]}
        if app["path"] in version_by_path
        else app
        for app in selected
    ]
    values = {
        "apps": json.dumps(selected, separators=(",", ":")),
        "app_count": str(len(selected)),
        "has_apps": str(bool(selected)).lower(),
        "release_matrix": json.dumps(
            {"include": versioned_releases}, separators=(",", ":")
        ),
        "has_releases": str(bool(versioned_releases)).lower(),
        "reason": reason,
    }
    write_github_output(values)
    print(f"Selected {len(selected)} firmware applications due to {reason}.")
    if changed:
        print(f"Changed paths: {len(changed)}")
    return 0


def platform_environment(config: dict, platform: str) -> dict[str, str]:
    definition = config["platforms"][platform]
    root_path = str(ROOT)
    if os.name == "nt" and shutil.which("cygpath"):
        root_path = subprocess.run(
            ["cygpath", "-u", str(ROOT)],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
        ).stdout.strip()
    env = os.environ.copy()
    env.update(
        {
            "MIOS32_PATH": root_path,
            "MIOS32_BIN_PATH": f"{root_path}/bin",
            "MIOS32_FAMILY": definition["family"],
            "MIOS32_PROCESSOR": definition["processor"],
            "MIOS32_BOARD": definition["board"],
            "MIOS32_LCD": "universal",
            "MIOS32_GCC_PREFIX": "arm-none-eabi",
            "MIOS32_SHELL": "/bin/bash" if os.name != "nt" else "sh",
        }
    )
    return env


def run_streamed(command: list[str], cwd: Path, env: dict[str, str]) -> int:
    process = subprocess.Popen(
        command,
        cwd=cwd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
        bufsize=1,
    )
    assert process.stdout is not None
    for line in process.stdout:
        print(line, end="")
    return process.wait()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def append_summary(lines: list[str]) -> None:
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not summary_path:
        return
    with open(summary_path, "a", encoding="utf-8") as summary:
        summary.write("\n".join(lines) + "\n")


def command_build(args: argparse.Namespace) -> int:
    config = load_config()
    selected = json.loads(os.environ.get("MIOS32_CI_APPS", "[]"))
    selected = [app for app in selected if args.platform in app["platforms"]]
    base_env = platform_environment(config, args.platform)
    make = shutil.which("make")
    if not make:
        raise RuntimeError("GNU make is not available")

    dist = (ROOT / args.dist).resolve()
    successes: list[dict] = []
    failures: list[str] = []

    print(f"Building {len(selected)} applications for {args.platform} with serial GNU make.")
    for index, app in enumerate(selected, start=1):
        directory = ROOT / app["path"]
        env = base_env.copy()
        env["MIOS32_LCD"] = config.get("lcd_overrides", {}).get(
            app["path"], base_env["MIOS32_LCD"]
        )
        if app.get("release_version"):
            env["MIOS32_RELEASE_VERSION"] = app["release_version"]
        print(f"\n[{index}/{len(selected)}] {app['path']} ({args.platform})")
        subprocess.run([make, "cleanall"], cwd=directory, env=env, check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        status = run_streamed([make], directory, env)
        if status:
            failures.append(app["path"])
            continue

        images = sorted(directory.glob("*.hex")) + sorted(directory.glob("*.bin"))
        if not images:
            failures.append(f"{app['path']} (no firmware image produced)")
            continue

        destination = dist / app["id"] / args.platform
        destination.mkdir(parents=True, exist_ok=True)
        copied = []
        for image in images:
            target = destination / image.name
            shutil.copy2(image, target)
            copied.append(
                {
                    "name": target.name,
                    "sha256": sha256(target),
                    "bytes": target.stat().st_size,
                }
            )
        successes.append({"app": app, "images": copied})
        subprocess.run([make, "cleanall"], cwd=directory, env=env, check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    manifest_dir = dist / "manifests"
    manifest_dir.mkdir(parents=True, exist_ok=True)
    (manifest_dir / f"{args.platform}.json").write_text(
        json.dumps(
            {
                "platform": args.platform,
                "successful": successes,
                "failed": failures,
            },
            indent=2,
        ) + "\n",
        encoding="utf-8",
    )

    summary = [
        f"## Firmware: {args.platform}",
        "",
        f"- Successful: {len(successes)}",
        f"- Failed: {len(failures)}",
    ]
    if failures:
        summary.extend(["", "Failed applications:", ""] + [f"- `{item}`" for item in failures])
    append_summary(summary)
    if failures:
        print("\nFailed applications:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    return 0


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    select = subparsers.add_parser("select")
    select.add_argument("--base", default="")
    select.add_argument("--head", default="HEAD")
    select.add_argument("--all", action="store_true")
    select.set_defaults(handler=command_select)

    build = subparsers.add_parser("build")
    build.add_argument("--platform", required=True, choices=("lpc17", "stm32", "stm32f4"))
    build.add_argument("--dist", default="dist/firmware")
    build.set_defaults(handler=command_build)
    return parser


def main() -> int:
    args = make_parser().parse_args()
    return args.handler(args)


if __name__ == "__main__":
    raise SystemExit(main())
