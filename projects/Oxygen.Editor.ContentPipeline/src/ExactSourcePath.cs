// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Finds a source under its exact name. Asset identities compare with their case, so on a file system that does not,
/// a source renamed only in case is gone under its old name although a lookup by that name still finds the file.
/// </summary>
internal static class ExactSourcePath
{
    /// <summary>Gets whether a file or folder exists under exactly the given name.</summary>
    /// <param name="fileSystem">The file system that holds the source.</param>
    /// <param name="root">The folder the relative path starts from; its own name is not compared.</param>
    /// <param name="relativePath">The source path below <paramref name="root"/>, every part compared with its case.</param>
    /// <returns><see langword="true"/> when every part of the path exists with the same case.</returns>
    public static bool Exists(IFileSystem fileSystem, string root, string relativePath)
    {
        var folder = root;
        foreach (var name in relativePath.Split(['/', '\\'], StringSplitOptions.RemoveEmptyEntries))
        {
            if (!fileSystem.Directory.Exists(folder)
                || !fileSystem.Directory.EnumerateFileSystemEntries(folder, name)
                    .Any(entry => string.Equals(Path.GetFileName(entry), name, StringComparison.Ordinal)))
            {
                return false;
            }

            folder = Path.Combine(folder, name);
        }

        return true;
    }
}
