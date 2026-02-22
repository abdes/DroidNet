"""Patch CMakeLists.txt to add PhysicsSceneLoader_test.cpp to the Loaders test program."""
import pathlib, sys

p = pathlib.Path(__file__).with_name("CMakeLists.txt")
text = p.read_bytes().decode("utf-8", errors="replace")

old = '"Mocks/MockStream.h"'
insert = '"Mocks/MockStream.h"\n    "PhysicsSceneLoader_test.cpp"'

if '"PhysicsSceneLoader_test.cpp"' in text:
    print("Already present — nothing to patch")
    sys.exit(0)

if old not in text:
    print("ERROR: anchor not found in CMakeLists.txt")
    sys.exit(1)

patched = text.replace(old, insert, 1)
p.write_bytes(patched.encode("utf-8"))
print("Patched OK")
