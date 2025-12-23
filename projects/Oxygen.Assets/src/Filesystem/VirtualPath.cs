// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Filesystem;

/// <summary>
/// Provides utility methods for handling Virtual Paths (forward-slash standardized paths).
/// </summary>
public static class VirtualPath
{
    /// <summary>
    /// Normalizes path separators to forward slashes ('/').
    /// </summary>
    /// <param name="path">The path to normalize.</param>
    /// <returns>The normalized path with forward slashes.</returns>
    public static string NormalizeSlashes(string path) => path.Replace('\\', '/');

    /// <summary>
    /// Normalizes a virtual path by resolving '.' and '..' segments and ensuring forward slashes.
    /// </summary>
    /// <param name="path">The virtual path to normalize.</param>
    /// <returns>A normalized virtual path.</returns>
    public static string Normalize(string path)
    {
        var input = NormalizeSlashes(path);
        var parts = input.Split('/', StringSplitOptions.RemoveEmptyEntries);
        var stack = new Stack<string>(parts.Length);

        foreach (var part in parts)
        {
            if (string.Equals(part, ".", StringComparison.Ordinal))
            {
                continue;
            }

            if (string.Equals(part, "..", StringComparison.Ordinal))
            {
                if (stack.Count > 0)
                {
                    _ = stack.Pop();
                }

                continue;
            }

            stack.Push(part);
        }

        if (stack.Count == 0)
        {
            return string.Empty;
        }

        var arr = stack.ToArray();
        Array.Reverse(arr);
        return string.Join('/', arr);
    }

    /// <summary>
    /// Combines two path strings into a virtual path using forward slashes.
    /// </summary>
    /// <param name="left">The first path.</param>
    /// <param name="right">The second path.</param>
    /// <returns>The combined virtual path.</returns>
    public static string Combine(string left, string right)
        => string.IsNullOrEmpty(left)
            ? NormalizeSlashes(right)
            : string.IsNullOrEmpty(right)
                ? NormalizeSlashes(left)
                : NormalizeSlashes(Path.Combine(left, right));

    /// <summary>
    /// Returns the directory information for the specified path string.
    /// </summary>
    /// <param name="path">The path of a file or directory.</param>
    /// <returns>The directory information for path, or an empty string if path denotes a root directory or is null. Returns path string with forward slashes.</returns>
    public static string GetDirectory(string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        var dir = Path.GetDirectoryName(path);
        return string.IsNullOrEmpty(dir) ? string.Empty : NormalizeSlashes(dir);
    }

    /// <summary>
    /// Returns the file name without the extension of a file path.
    /// </summary>
    /// <param name="path">The path of a file.</param>
    /// <returns>The file name without extension.</returns>
    public static string GetFileNameWithoutExtension(string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return Path.GetFileNameWithoutExtension(path);
    }
}
