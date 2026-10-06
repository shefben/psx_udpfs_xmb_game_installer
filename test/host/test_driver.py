#!/usr/bin/env python3
"""Regression checks for the shipped HDD driver (vendor/irx/ps2hdd-hdl.irx).

The shipped driver is a source build (tools/driver/make-driver.sh):
  - the UNMODIFIED source of HDLGameInstaller cdc6636 rebuilt in the
    pinned legacy toolchain reproduces the vendored upstream IRX
    byte-for-byte (checked by make-driver.sh at build time);
  - the shipped IRX is the same build with
    patches/apa-hdl/0001-allow-removing-hidden-hdl-games.patch.

This test pins both hashes, checks the patch contents, and checks the
committed reference disassembly of the "__" checks against the binary
(when an IOP objdump is available). The removal rule itself is unit-
tested in C against tools/driver/remove_policy.h (test_policy.c).
"""
import hashlib
import os
import shutil
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
fails = 0


def check(name, ok, detail=""):
    global fails
    print("%s driver_%s%s" % ("ok  " if ok else "FAIL", name, (" -- " + detail) if not ok and detail else ""))
    if not ok:
        fails += 1


def env():
    vals = {}
    for line in open(os.path.join(ROOT, "tools/driver/driver.env")):
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            k, v = line.split("=", 1)
            vals[k] = v
    return vals


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def main():
    e = env()
    shipped = os.path.join(ROOT, "vendor/irx/ps2hdd-hdl.irx")
    upstream = os.path.join(ROOT, "reference/HDLGameInstaller/irx/ps2hdd-hdl.irx")

    check("shipped_hash_pinned", sha(shipped) == e["DRIVER_PATCHED_SHA256"], sha(shipped))
    if os.path.exists(upstream):
        check("upstream_hash_pinned", sha(upstream) == e["DRIVER_UPSTREAM_SHA256"], sha(upstream))
    check("shipped_differs_from_upstream", e["DRIVER_PATCHED_SHA256"] != e["DRIVER_UPSTREAM_SHA256"])

    patch = open(os.path.join(ROOT, "patches/apa-hdl/0001-allow-removing-hidden-hdl-games.patch")).read()
    hunks = [l for l in patch.splitlines() if l.startswith(("+", "-")) and not l.startswith(("+++", "---"))]
    removed = [l for l in hunks if l.startswith("-")]
    check("patch_removes_only_dunder_check",
          removed == ["-\tif(id[0]=='_' && id[1]=='_')"], repr(removed))
    check("patch_adds_name_and_type_policy",
          any("apaRemoveNameAllowed(id)" in l for l in hunks) and
          any("apaRemoveTypeAllowed(id, clink->header->type)" in l for l in hunks) and
          any('#include "remove_policy.h"' in l for l in hunks))

    ren = open(os.path.join(ROOT, "patches/apa-hdl/0002-allow-renaming-hidden-hdl-games.patch")).read()
    rh = [l for l in ren.splitlines() if l.startswith(("+", "-")) and not l.startswith(("+++", "---"))]
    check("rename_patch_removes_only_dunder_check",
          [l for l in rh if l.startswith("-")] ==
          ["-\t// Do not allow system partitions (__*) to be renamed.",
           "-\tif(oldParams->id[0]=='_' && oldParams->id[1]=='_')"],
          repr([l for l in rh if l.startswith("-")]))
    check("rename_patch_uses_same_policy",
          any("apaRemoveNameAllowed(oldParams->id)" in l for l in rh) and
          any("apaRemoveTypeAllowed(oldParams->id, clink->header->type)" in l for l in rh))

    pol = open(os.path.join(ROOT, "tools/driver/remove_policy.h")).read()
    check("policy_requires_dot_and_hdl_type",
          "id[2] == '.'" in pol and "0x1337" in pol)

    # Reference disassembly of the '_' comparison sites.
    for which, path in (("upstream", upstream), ("patched", shipped)):
        doc = os.path.join(ROOT, "docs/driver/apaRemove-%s.txt" % which)
        check("disasm_doc_%s_present" % which, os.path.exists(doc))
        if not os.path.exists(doc) or not os.path.exists(path):
            continue
        text = open(doc).read()
        check("disasm_doc_%s_hash" % which, ("sha256 " + sha(path)) in text)
        if shutil.which(os.environ.get("IOP_OBJDUMP", "mipsel-none-elf-objdump")):
            out = subprocess.run([sys.executable, os.path.join(ROOT, "tools/driver/disasm-remove.py"), path],
                                 capture_output=True, text=True, check=True).stdout
            check("disasm_doc_%s_current" % which, out == text)
    if os.path.exists(os.path.join(ROOT, "docs/driver/apaRemove-patched.txt")):
        t = open(os.path.join(ROOT, "docs/driver/apaRemove-patched.txt")).read()
        check("patched_disasm_has_dot_and_hdl_checks", "0x2e" in t and "0x1337" in t)
        u = open(os.path.join(ROOT, "docs/driver/apaRemove-upstream.txt")).read()
        check("upstream_disasm_has_no_policy", "0x1337" not in u)

    print("\ndriver: %s" % ("all passed" if not fails else "%d failed" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
