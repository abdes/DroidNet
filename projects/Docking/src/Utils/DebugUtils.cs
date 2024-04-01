// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Utils;

using System.Diagnostics;

/// <summary>Contains extension methods, providing helpers for debugging and diagnostics.</summary>
public static class DebugUtils
{
    /// <summary>
    /// Dump to the contents of a docking tree starting at the specified
    /// <paramref name="group">dock group</paramref>
    /// to the Debug
    /// output.
    /// </summary>
    /// <param name="group">The starting group of the tree to dump.</param>
    /// <param name="indentChar">The character used to indent children relative to their parent. Default is <c>' '</c>.</param>
    /// <param name="indentSize">The number of indent characters to use per indentation level. Default is <c>3</c>.</param>
    /// <param name="initialIndentLevel">Can be used to specify an initial indentation for the dumped info.</param>
    public static void DumpGroup(
        this IDockGroup group,
        char indentChar = ' ',
        int indentSize = 3,
        int initialIndentLevel = 0)
        => DumpGroupRecursive(group, initialIndentLevel, indentChar, indentSize);

    private static void DumpGroupRecursive(IDockGroup? item, int indentLevel, char indentChar, int indentSize)
    {
        if (item is null)
        {
            return;
        }

        var indent = new string(indentChar, indentLevel * indentSize); // 2 spaces per indent level
        Debug.WriteLine($"{indent}{item}");
        DumpGroupRecursive(item.First, indentLevel + 1, indentChar, indentSize);
        DumpGroupRecursive(item.Second, indentLevel + 1, indentChar, indentSize);
    }
}
