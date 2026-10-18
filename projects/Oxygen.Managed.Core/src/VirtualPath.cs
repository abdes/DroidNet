// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core;

/// <summary>
/// Provides utility methods for handling virtual paths (forward-slash standardized paths).
/// </summary>
public static partial class VirtualPath
{
    /// <summary>
    /// Creates a virtual path from a mount name and a path within that mount. The mount name must pass
    /// <see cref="ValidateMountName(string)"/> and every segment of the relative path <see cref="ValidateSegment(string)"/>.
    /// </summary>
    /// <param name="mountPointName">The mount name (first path segment).</param>
    /// <param name="relativePath">A relative path within the mount (may include '/' or '' separators).</param>
    /// <returns>The virtual path, such as <c>/Content/Materials/Red.omat</c>, or <c>/Content</c> without a relative path.</returns>
    /// <exception cref="ArgumentException">The mount name or a segment is refused; the message holds the refusal code.</exception>
    public static string CreateAbsolute(string mountPointName, string? relativePath = null)
    {
        ArgumentNullException.ThrowIfNull(mountPointName);
        if (ValidateMountName(mountPointName) is { } mountError)
        {
            throw new ArgumentException($"The mount name is refused ({mountError.ToCode()}).", nameof(mountPointName));
        }

        if (string.IsNullOrEmpty(relativePath))
        {
            return "/" + mountPointName;
        }

        var segments = NormalizeSlashes(relativePath).Split('/');
        foreach (var segment in segments)
        {
            if (ValidateSegment(segment) is { } error)
            {
                throw new ArgumentException($"The relative path is refused ({error.ToCode()}).", nameof(relativePath));
            }
        }

        return "/" + mountPointName + "/" + string.Join('/', segments);
    }

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
        if (string.IsNullOrEmpty(path))
        {
            return string.Empty;
        }

        var input = NormalizeSlashes(path);
        var parts = input.Split('/', StringSplitOptions.RemoveEmptyEntries);
        var stack = new List<string>(parts.Length);

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
                    stack.RemoveAt(stack.Count - 1);
                }

                continue;
            }

            stack.Add(part);
        }

        return stack.Count == 0 ? string.Empty : string.Join('/', stack);
    }

    /// <summary>
    /// Combines two path strings into a virtual path using forward slashes.
    /// </summary>
    /// <param name="left">The first path.</param>
    /// <param name="right">The second path.</param>
    /// <returns>The combined virtual path.</returns>
    public static string Combine(string left, string right)
    {
        if (string.IsNullOrEmpty(left))
        {
            return NormalizeSlashes(right);
        }

        if (string.IsNullOrEmpty(right))
        {
            return NormalizeSlashes(left);
        }

        var normalizedLeft = NormalizeSlashes(left);
        var normalizedRight = NormalizeSlashes(right);

        return normalizedLeft.EndsWith('/')
            ? normalizedLeft + normalizedRight
            : normalizedLeft + '/' + normalizedRight;
    }

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
