import clang.cindex
import os
import sys

# Locate libclang
common_paths = [
    r"C:\Program Files\LLVM\bin",
    r"C:\Program Files\LLVM\lib",
]
for path in common_paths:
    if os.path.exists(os.path.join(path, "libclang.dll")):
        clang.cindex.Config.set_library_path(path)
        break

def dump_node(node, indent=0):
    loc = node.location
    usr = node.get_usr()
    ref = node.referenced
    ref_usr = ref.get_usr() if ref else "None"
    print(f"{'  ' * indent}{node.kind} : spelling='{node.spelling}', loc={loc.line}:{loc.column}, usr='{usr}', ref_usr='{ref_usr}'")
    for child in node.get_children():
        dump_node(child, indent + 1)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python debug_ast.py <file>")
        sys.exit(1)
    file_path = os.path.abspath(sys.argv[1])
    index = clang.cindex.Index.create()
    tu = index.parse(file_path, args=['-std=c++23'])
    print(f"Diagnostics: {list(tu.diagnostics)}")
    dump_node(tu.cursor)
