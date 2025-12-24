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
    /// Determines whether a path is a canonical absolute virtual path.
    /// Canonical absolute virtual paths:
    /// <list type="bullet">
    /// <item><description>Start with '/'.</description></item>
    /// <item><description>Use '/' separators only (no '\').</description></item>
    /// <item><description>Do not contain empty segments ('//').</description></item>
    /// <item><description>Do not contain '.' or '..' segments.</description></item>
    /// </list>
    /// </summary>
    /// <param name="path">The path to check.</param>
    /// <returns><see langword="true"/> when the path is canonical; otherwise <see langword="false"/>.</returns>
    public static bool IsCanonicalAbsolute(string path)
    {
        if (string.IsNullOrEmpty(path))
        {
            return false;
        }

        if (string.Equals(path, "/", StringComparison.Ordinal))
        {
            return true;
        }

        if (path[0] != '/')
        {
            return false;
        }

        if (path.Contains('\\', StringComparison.Ordinal))
        {
            return false;
        }

        // Reject trailing slash (would imply an empty segment).
        if (path[^1] == '/')
        {
            return false;
        }

        // Split with empty segments preserved to detect "//".
        var segments = path.Split('/', StringSplitOptions.None);
        if (segments.Length < 2)
        {
            return false;
        }

        // segments[0] is empty due to leading '/'.
        for (var i = 1; i < segments.Length; i++)
        {
            var segment = segments[i];
            if (segment.Length == 0)
            {
                return false;
            }

            if (string.Equals(segment, ".", StringComparison.Ordinal)
                || string.Equals(segment, "..", StringComparison.Ordinal))
            {
                return false;
            }
        }

        return true;
    }

    /// <summary>
    /// Creates a canonical absolute virtual path from a mount point name and a relative path within that mount.
    /// The resulting path is guaranteed to be canonical according to <see cref="IsCanonicalAbsolute"/>.
    /// </summary>
    /// <param name="mountPointName">The mount point name (first path segment).</param>
    /// <param name="relativePath">A relative path within the mount (may include '/' or '\' separators).</param>
    /// <returns>A canonical absolute virtual path.</returns>
    /// <exception cref="ArgumentException">Thrown when inputs are invalid.</exception>
    public static string CreateAbsolute(string mountPointName, string? relativePath = null)
    {
        ArgumentException.ThrowIfNullOrEmpty(mountPointName);

        if (mountPointName.Contains('/', StringComparison.Ordinal)
            || mountPointName.Contains('\\', StringComparison.Ordinal)
            || string.Equals(mountPointName, ".", StringComparison.Ordinal)
            || string.Equals(mountPointName, "..", StringComparison.Ordinal))
        {
            throw new ArgumentException("Mount point name must be a single, non-dot segment.", nameof(mountPointName));
        }

        if (string.IsNullOrEmpty(relativePath))
        {
            return "/" + mountPointName;
        }

        var normalizedRelative = NormalizeSlashes(relativePath);
        if (normalizedRelative.StartsWith('/'))
        {
            throw new ArgumentException("Relative path must not start with '/'.", nameof(relativePath));
        }

        if (normalizedRelative.EndsWith('/'))
        {
            throw new ArgumentException("Relative path must not end with '/'.", nameof(relativePath));
        }

        // Preserve empty segments to detect "//".
        var parts = normalizedRelative.Split('/', StringSplitOptions.None);
        foreach (var part in parts)
        {
            if (part.Length == 0)
            {
                throw new ArgumentException("Relative path must not contain empty segments ('//').", nameof(relativePath));
            }

            if (string.Equals(part, ".", StringComparison.Ordinal)
                || string.Equals(part, "..", StringComparison.Ordinal))
            {
                throw new ArgumentException("Relative path must not contain '.' or '..' segments.", nameof(relativePath));
            }
        }

        var result = "/" + mountPointName + "/" + normalizedRelative;
        return !IsCanonicalAbsolute(result)
            ? throw new ArgumentException("Inputs did not produce a canonical absolute virtual path.", nameof(relativePath))
            : result;
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
