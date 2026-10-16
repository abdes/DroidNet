// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>The file a document edits: where it is on disk and the asset it is.</summary>
public sealed record DocumentFile
{
    /// <summary>Initializes a new instance of the <see cref="DocumentFile"/> class.</summary>
    /// <param name="path">The file's path; it is made absolute.</param>
    /// <param name="assetUri">The asset URI of the file.</param>
    public DocumentFile(string path, Uri assetUri)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        ArgumentNullException.ThrowIfNull(assetUri);
        this.Path = System.IO.Path.GetFullPath(path);
        this.AssetUri = assetUri;
    }

    /// <summary>Gets the file's absolute path.</summary>
    public string Path { get; }

    /// <summary>Gets the asset URI of the file.</summary>
    public Uri AssetUri { get; }

    /// <summary>Tests whether a path names this file, ignoring case as the file system does.</summary>
    /// <param name="path">The path to compare.</param>
    /// <returns>Whether both paths name the same file.</returns>
    public bool IsAt(string path)
        => string.Equals(this.Path, System.IO.Path.GetFullPath(path), StringComparison.OrdinalIgnoreCase);
}
