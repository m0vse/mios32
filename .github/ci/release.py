#!/usr/bin/env python3
"""Application-scoped version and release packaging helpers."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = Path(__file__).with_name("firmware-config.json")


def numeric_version(value: str) -> tuple[int, ...]:
    if not re.fullmatch(r"\d+(?:\.\d+)+", value):
        raise ValueError(f"Unsupported numeric version: {value}")
    return tuple(int(part) for part in value.split("."))


def version_text(parts: tuple[int, ...], template: str) -> str:
    widths = [len(part) for part in template.split(".")]
    rendered = []
    for index, part in enumerate(parts):
        width = widths[index] if index < len(widths) else 1
        rendered.append(f"{part:0{width}d}")
    return ".".join(rendered)


def tag_commit(tag: str) -> str:
    result = subprocess.run(
        ["git", "rev-list", "-n", "1", tag],
        cwd=ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    return result.stdout.strip() if result.returncode == 0 else ""


def next_version(app: str, base: str, commit: str) -> str:
    prefix = f"{app}-v"
    result = subprocess.run(
        ["git", "tag", "--list", f"{prefix}*"],
        cwd=ROOT,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )
    versions = []
    for tag in result.stdout.splitlines():
        value = tag.removeprefix(prefix)
        try:
            parsed = numeric_version(value)
        except ValueError:
            continue
        versions.append((parsed, value, tag))

    if not versions:
        return base
    _, latest_text, latest_tag = max(versions, key=lambda item: item[0])
    if commit and tag_commit(latest_tag) == commit:
        return latest_text

    base_parts = numeric_version(base)
    latest_parts = numeric_version(latest_text)
    current = max(base_parts, latest_parts)
    bumped = current[:-1] + (current[-1] + 1,)
    return version_text(bumped, latest_text if latest_parts >= base_parts else base)


def command_next_version(args: argparse.Namespace) -> int:
    print(next_version(args.app, args.base, args.commit))
    return 0


def copy_release_assets(source: Path, patterns: list[str], destination: Path) -> None:
    for pattern in patterns:
        for item in source.glob(pattern):
            target = destination / item.name
            if item.is_dir():
                shutil.copytree(item, target, dirs_exist_ok=True)
            else:
                shutil.copy2(item, target)


def command_package_firmware(args: argparse.Namespace) -> int:
    config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    release_app = next(app for app in config["release_apps"] if app["id"] == args.app)
    platforms = config["release_platforms"]
    dist = (ROOT / args.dist).resolve()
    output = (ROOT / args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    archive_base = f"{args.app}-v{args.version}"

    with tempfile.TemporaryDirectory(dir=output) as temporary:
        package = Path(temporary) / archive_base
        package.mkdir()
        firmware = package / "firmware"
        for platform in platforms:
            source = dist / args.app / platform
            if not source.is_dir():
                raise FileNotFoundError(f"Missing {args.app} firmware for {platform}: {source}")
            shutil.copytree(source, firmware / platform)
        copy_release_assets(
            ROOT / release_app["path"], release_app.get("assets", []), package
        )
        metadata = {
            "application": args.app,
            "version": args.version,
            "commit": args.commit,
            "platforms": platforms,
        }
        (package / "release.json").write_text(
            json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
        )
        archive = shutil.make_archive(str(output / archive_base), "zip", package.parent, package.name)
    print(archive)
    return 0


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    version = subparsers.add_parser("next-version")
    version.add_argument("--app", required=True)
    version.add_argument("--base", required=True)
    version.add_argument("--commit", default="")
    version.set_defaults(handler=command_next_version)

    package = subparsers.add_parser("package-firmware")
    package.add_argument("--app", required=True)
    package.add_argument("--version", required=True)
    package.add_argument("--commit", required=True)
    package.add_argument("--dist", default="dist/firmware")
    package.add_argument("--output", default="dist/releases")
    package.set_defaults(handler=command_package_firmware)
    return parser


def main() -> int:
    args = make_parser().parse_args()
    return args.handler(args)


if __name__ == "__main__":
    raise SystemExit(main())
