#!/usr/bin/env python3
"""Regenerate windows-cross-msys2-toolchain-arm64-llvm-windres.patch from _wc_*.cmake."""
from pathlib import Path
import difflib

root = Path(__file__).resolve().parent
a = root.joinpath("_wc_upstream.cmake").read_text(encoding="utf-8").splitlines(True)
b = root.joinpath("_wc_patched.cmake").read_text(encoding="utf-8").splitlines(True)
d = list(
    difflib.unified_diff(
        a, b, fromfile="a/msys2.toolchain.cmake", tofile="b/msys2.toolchain.cmake"
    )
)
root.joinpath("windows-cross-msys2-toolchain-arm64-llvm-windres.patch").write_text(
    "".join(d), encoding="utf-8", newline="\n"
)
print("wrote", len(d), "lines")
