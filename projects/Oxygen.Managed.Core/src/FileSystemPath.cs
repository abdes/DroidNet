// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Managed.Core;

/// <summary>
/// A file or folder on disk, as a full path. This type owns how physical paths compare: full-path normalization, no
/// trailing separator, and the file system's case rule (case-insensitive on Windows).
/// </summary>
[JsonConverter(typeof(FileSystemPathJsonConverter))]
public sealed class FileSystemPath : IEquatable<FileSystemPath>
{
    private FileSystemPath(string fullPath) => this.FullPath = fullPath;

    /// <summary>Gets how the file system compares names: case-insensitively on Windows, ordinally elsewhere.</summary>
    public static StringComparison NameComparison { get; } = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;

    /// <summary>Gets the comparer of names and relative paths on disk, by <see cref="NameComparison"/>.</summary>
    public static StringComparer NameComparer { get; } = StringComparer.FromComparison(NameComparison);

    /// <summary>Gets the full path, without a trailing separator unless it is a root.</summary>
    public string FullPath { get; }

    /// <summary>Gets the last segment: the file or folder name; empty for a root.</summary>
    public string Name => Path.GetFileName(this.FullPath);

    /// <summary>Gets the folder that holds this path, or <see langword="null"/> for a root.</summary>
    public FileSystemPath? Parent => Path.GetDirectoryName(this.FullPath) is { } parent ? new(parent) : null;

    /// <summary>Tests whether two paths name the same file or folder.</summary>
    /// <param name="left">The first path.</param>
    /// <param name="right">The second path.</param>
    /// <returns><see langword="true"/> when both are null or both name the same file or folder.</returns>
    public static bool operator ==(FileSystemPath? left, FileSystemPath? right) => left?.Equals(right) ?? right is null;

    /// <summary>Tests whether two paths name different files or folders.</summary>
    /// <param name="left">The first path.</param>
    /// <param name="right">The second path.</param>
    /// <returns><see langword="true"/> when the paths differ.</returns>
    public static bool operator !=(FileSystemPath? left, FileSystemPath? right) => !(left == right);

    /// <summary>Gets the full path of a file or folder.</summary>
    /// <param name="path">An absolute path, or a path relative to the current directory.</param>
    /// <returns>The path.</returns>
    /// <exception cref="ArgumentException"><paramref name="path"/> is empty or not a valid path.</exception>
    public static FileSystemPath From(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        var full = Path.GetFullPath(path);
        var trimmed = Path.TrimEndingDirectorySeparator(full);
        return new(trimmed);
    }

    /// <summary>Gets the path of a file or folder below this one.</summary>
    /// <param name="relativePath">The path below this one; <c>/</c> and <c>\</c> both separate segments.</param>
    /// <returns>The path.</returns>
    public FileSystemPath Combine(string relativePath)
    {
        ArgumentNullException.ThrowIfNull(relativePath);
        return From(Path.Combine(this.FullPath, relativePath));
    }

    /// <summary>Tests whether this path is a folder or lies inside it.</summary>
    /// <param name="folder">The folder.</param>
    /// <returns><see langword="true"/> when this path is <paramref name="folder"/> or one of its descendants.</returns>
    public bool IsSameOrInside(FileSystemPath folder)
    {
        ArgumentNullException.ThrowIfNull(folder);
        if (!this.FullPath.StartsWith(folder.FullPath, NameComparison))
        {
            return false;
        }

        return this.FullPath.Length == folder.FullPath.Length
            || Path.EndsInDirectorySeparator(folder.FullPath)
            || this.FullPath[folder.FullPath.Length] == Path.DirectorySeparatorChar;
    }

    /// <summary>Gets the path below a folder this path lies inside, its segments separated by <c>/</c>.</summary>
    /// <param name="folder">The folder.</param>
    /// <returns>The path below <paramref name="folder"/>; empty for the folder itself.</returns>
    /// <exception cref="ArgumentException">This path is not inside <paramref name="folder"/>.</exception>
    public string GetPathBelow(FileSystemPath folder)
    {
        if (!this.IsSameOrInside(folder))
        {
            throw new ArgumentException($"'{this}' is not inside '{folder}'.", nameof(folder));
        }

        return this.FullPath.Length == folder.FullPath.Length
            ? string.Empty
            : this.FullPath[(folder.FullPath.Length + (Path.EndsInDirectorySeparator(folder.FullPath) ? 0 : 1))..].Replace(Path.DirectorySeparatorChar, '/');
    }

    /// <inheritdoc/>
    public bool Equals(FileSystemPath? other) => other is not null && string.Equals(this.FullPath, other.FullPath, NameComparison);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => this.Equals(obj as FileSystemPath);

    /// <inheritdoc/>
    public override int GetHashCode() => string.GetHashCode(this.FullPath, NameComparison);

    /// <summary>Gets the full path.</summary>
    /// <returns>The full path.</returns>
    public override string ToString() => this.FullPath;
}
