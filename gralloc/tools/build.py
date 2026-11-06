#!/usr/bin/env python3
"""Build the Android 14 guest allocator against libraries from a pinned image."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


HEADER_REVISIONS = {
    "libhardware": "32748ddb39cf99296ff35d374b15f04b4522e85c",
    "core": "b8e07c25c4190f4338a6ce0394497925128d2609",
    "logging": "0d18f57987b1dbf3494ca7d2b13645eac002d3cd",
}
TARGETS = {"arm64-v8a": "aarch64-linux-android", "x86_64": "x86_64-linux-android"}


def digest(path):
    content = path.read_bytes()
    checksum = hashlib.sha256(content)
    return checksum.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", required=True, type=Path)
    parser.add_argument("--headers", required=True, type=Path)
    parser.add_argument("--runtime-libs", required=True, type=Path)
    parser.add_argument("--abi", required=True, choices=TARGETS)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    script = Path(__file__)
    script = script.resolve()
    source = script.parents[1]
    headers = args.headers.resolve()
    libraries = args.runtime_libs.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    host = "darwin-x86_64" if sys.platform == "darwin" else "linux-x86_64"
    ndk = args.ndk.resolve()
    binaries = ndk / "toolchains/llvm/prebuilt" / host / "bin"
    compiler = binaries / (TARGETS[args.abi] + "34-clang++")
    compiler_path = str(compiler)
    for name, expected in HEADER_REVISIONS.items():
        checkout = headers / name
        result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=checkout, text=True, capture_output=True, check=True)
        actual = result.stdout.strip()
        if actual != expected:
            raise ValueError("Unexpected Android header revision: " + name)

    includes = [
        headers / "libhardware/include",
        headers / "core/libcutils/include",
        headers / "core/libsystem/include",
        headers / "logging/liblog/include",
    ]
    flags = ["-std=gnu++17", "-fPIC", "-O2", "-Wall", "-Werror", '-DLOG_TAG="gralloc"']
    for directory in includes:
        include_path = str(directory)
        flags.extend(["-I", include_path])
    objects = []
    source_hashes = {}
    for name in ["gralloc.cpp", "mapper.cpp", "framebuffer.cpp"]:
        file = source / name
        target = output / (file.stem + ".o")
        file_path = str(file)
        target_path = str(target)
        command = [compiler_path, *flags, "-c", file_path, "-o", target_path]
        subprocess.run(command, check=True)
        objects.append(target_path)
        source_hashes[name] = digest(file)
    for name in ["gr.h", "gralloc_priv.h"]:
        source_hashes[name] = digest(source / name)

    allocator = output / "gralloc.redroid.so"
    allocator_path = str(allocator)
    library_path = str(libraries)
    command = [
        compiler_path, "-shared", "-nostdlib++", "-Wl,-z,defs", "-Wl,-soname,gralloc.redroid.so",
        *objects, "-L", library_path, "-l:libcutils.so", "-l:libc++.so", "-llog", "-o", allocator_path,
    ]
    subprocess.run(command, check=True)
    allocator.chmod(0o644)
    probe = output / "hardware-buffer-test"
    probe_path = str(probe)
    probe_source = source / "tests/hardware_buffer_test.cpp"
    probe_source_path = str(probe_source)
    command = [
        compiler_path, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-static-libstdc++",
        probe_source_path, "-landroid", "-o", probe_path,
    ]
    subprocess.run(command, check=True)
    result = subprocess.run([compiler_path, "--version"], text=True, capture_output=True, check=True)
    version_lines = result.stdout.splitlines()
    compiler_version = version_lines[0]
    library_hashes = {}
    for name in ["libcutils.so", "libc++.so"]:
        library_hashes[name] = digest(libraries / name)
    record = {
        "abi": args.abi,
        "api": 34,
        "compiler": compiler_version,
        "headers": HEADER_REVISIONS,
        "sources": source_hashes,
        "runtime_libraries": library_hashes,
        "allocator_sha256": digest(allocator),
        "probe_sha256": digest(probe),
    }
    rendered = json.dumps(record, indent=2)
    record_path = output / "build.json"
    record_path.write_text(rendered + "\n")
    print(rendered)


if __name__ == "__main__":
    main()
