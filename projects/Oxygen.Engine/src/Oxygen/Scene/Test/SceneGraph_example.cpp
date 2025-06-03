//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>

#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

// When not building with shared libraries / DLLs, make sure to link the main
// executable (statically) with the type registry initialization library:
//
// target_link_libraries(
//   ${META_MODULE_TARGET}
//   PRIVATE
//     $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:oxygen::ts-init>
// )
//
// The linker will optimize the library out if it is not being used, so we force
// link it by calling its initialization function and storing the result in an
// unused variable.
namespace oxygen {
class TypeRegistry;
} // namespace oxygen
extern "C" auto InitializeTypeRegistry() -> oxygen::TypeRegistry*;
namespace {
[[maybe_unused]] const auto* const kTsRegistryUnused = InitializeTypeRegistry();
} // namespace

namespace {

// Helper to print a node's name and visibility, with ASCII tree structure
void PrintNodeInfo(
    const SceneNode& node,
    const std::string& prefix,
    const bool is_last, const bool is_root = false)
{
    std::cout << prefix;
    if (!is_root) {
        std::cout << (is_last ? "\\-- " : "|-- ");
    }
    const auto obj_opt = node.GetObject();
    if (obj_opt) {
        const auto& obj = obj_opt->get();
        std::cout << obj.GetName();
        std::cout << " [visible="
                  << (obj.GetFlags().GetEffectiveValue(SceneNodeFlags::kVisible)
                             ? "true"
                             : "false")
                  << "]\n";
    } else {
        std::cout << "<invalid node>\n";
    }
}

// Recursive tree printer with ASCII tree drawing
void PrintTree(
    const SceneNode& node,
    const std::string& prefix = "",
    const bool is_last = true,
    const bool is_root = true)
{
    PrintNodeInfo(node, prefix, is_last, is_root);
    // Collect children
    std::vector<SceneNode> children;
    auto child = node.GetFirstChild();
    while (child) {
        children.push_back(*child);
        child = child->GetNextSibling();
    }
    for (size_t i = 0; i < children.size(); ++i) {
        const bool last = (i == children.size() - 1);
        std::string child_prefix = prefix;
        if (!is_root) {
            child_prefix += (is_last ? "    " : "|   ");
        }
        PrintTree(children[i], child_prefix, last, false);
    }
}

// Helper for fatal error reporting
[[noreturn]] void PrintErrorAndExit(const std::string& msg)
{
    std::cerr << "[ERROR] " << msg << '\n';

    // Ensure output is flushed before termination
    std::cerr.flush();
    std::cout.flush();

    // Use std::quick_exit instead of std::exit for better thread safety
    // Or return an error code to the caller for more controlled shutdown
    std::quick_exit(EXIT_FAILURE);
}

// Helper for subsection dividers
void PrintSubSection(const std::string& title)
{
    std::cout << "\n-- " << title << " --\n";
}

// Helper for aligned status checks
void PrintStatus(
    const std::string& label,
    const std::string& value,
    const std::string& note = "")
{
    std::cout << "  - " << std::left << std::setw(28) << label << ": "
              << std::setw(6) << value
              << (note.empty() ? "" : "  (" + note + ")") << "\n";
}

} // namespace

auto main() -> int
{
    PrintSubSection("Creation");
    const auto scene = std::make_shared<Scene>("ExampleScene");
    std::cout << "  * Scene:         'ExampleScene'\n";
    const auto root = scene->CreateNode("Root");
    std::cout << "  * Root node:     'Root'\n";

    // Safe optional handling
    auto child1_opt = scene->CreateChildNode(root, "Child1");
    auto child2_opt = scene->CreateChildNode(root, "Child2");
    if (!child1_opt || !child2_opt) {
        PrintErrorAndExit("Failed to create child nodes");
    }

    // Store references to nodes after checking
    SceneNode& child1 = *child1_opt;
    SceneNode& child2 = *child2_opt;
    std::cout << "  * Children:      'Child1', 'Child2'\n";

    // Safe grandchild creation
    auto grandchild_opt = scene->CreateChildNode(child1, "Grandchild");
    if (!grandchild_opt) {
        PrintErrorAndExit("Failed to create Grandchild");
    }
    SceneNode& grandchild = *grandchild_opt;
    std::cout << "  * Grandchild:    'Grandchild' (under 'Child1')\n";

    // Safe object access
    const auto child2_obj = child2.GetObject();
    if (child2_obj) {
        child2_obj->get().SetName("SecondChild");
        std::cout << "  * Renamed:       'Child2' -> 'SecondChild'\n";
    } else {
        PrintErrorAndExit("Failed to get object for Child2");
    }

    // Safe flag access
    const auto child1_obj_opt = child1.GetObject();
    if (!child1_obj_opt) {
        PrintErrorAndExit("Failed to get object for Child1");
    }

    auto& child1_flags = child1_obj_opt->get().GetFlags();
    child1_flags.SetLocalValue(oxygen::scene::SceneNodeFlags::kVisible, false);
    std::cout << "  * Set 'Child1' visibility: false\n";

    PrintSubSection("Node Status Checks");
    PrintStatus("Is 'grandchild' valid?",
        grandchild.IsValid() ? "yes" : "no",
        grandchild.IsValid() ? "ok" : "error");
    PrintStatus("Is 'root' a root node?",
        root.IsRoot() ? "yes" : "no",
        root.IsRoot() ? "ok" : "error");
    PrintStatus("Is 'Child1' a root node?",
        !child1.IsRoot() ? "no" : "yes",
        !child1.IsRoot() ? "ok" : "error");
    PrintStatus("Is 'Child1' visible?",
        child1_flags.GetEffectiveValue(SceneNodeFlags::kVisible) ? "yes" : "no",
        child1_flags.GetEffectiveValue(SceneNodeFlags::kVisible) ? "error" : "ok");

    PrintSubSection("Scene Hierarchy");
    PrintTree(root);

    PrintSubSection("Parent Lookup");
    std::cout << "  Parent of 'Grandchild': ";
    if (const auto parent_opt = grandchild.GetParent()) {
        // Safe parent access
        const auto& parent = *parent_opt;
        const auto parent_obj = parent.GetObject();
        if (parent_obj && parent_obj->get().GetName() == "Child1") {
            std::cout << parent_obj->get().GetName() << " (ok)\n";
        } else {
            std::cout << "(wrong parent)\n";
        }
    } else {
        std::cout << "(not found)\n";
    }

    PrintSubSection("Destroying 'Child1' subtree...");
    scene->DestroyNodeHierarchy(child1);

    PrintSubSection("Scene hierarchy after deletion");
    PrintTree(root);

    PrintSubSection("Post-Deletion Checks (Lazy Invalidation)");
    // Check validity before access (should be valid due to lazy invalidation)
    PrintStatus("Is 'Grandchild' valid?  (before access)",
        grandchild.IsValid() ? "yes" : "no",
        grandchild.IsValid() ? "ok" : "error");
    std::cout << "  - Accessing 'Grandchild' object: ";
    const auto grandchild_obj = grandchild.GetObject();
    if (!grandchild_obj) {
        std::cout << "object not found  (ok)\n";
    } else {
        std::cout << grandchild_obj->get().GetName() << " (error)\n";
    }
    // Check validity after access (should now be invalid)
    PrintStatus("Is 'Grandchild' valid?   (after access)",
        grandchild.IsValid() ? "yes" : "no",
        grandchild.IsValid() ? "error" : "ok");

    return EXIT_SUCCESS;
}
