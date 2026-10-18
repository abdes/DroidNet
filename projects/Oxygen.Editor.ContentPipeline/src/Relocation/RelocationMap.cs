// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// What one relocation changes: the physical moves, the asset identities they move, and the virtual paths of the
/// files and folders that move.
/// </summary>
internal sealed class RelocationMap
{
    private readonly List<(AssetPath Source, AssetPath Target, bool IsFolder)> identities = [];
    private readonly List<(AssetPath Source, AssetPath Target, bool IsFolder)> paths = [];
    private readonly List<RelocationFileMove> moves = [];

    /// <summary>Gets the physical moves in the order they apply.</summary>
    public IReadOnlyList<RelocationFileMove> Moves => this.moves;

    /// <summary>Gets the identity moves: files map exactly, folders map everything below them.</summary>
    public IEnumerable<(AssetPath Source, AssetPath Target, bool IsFolder)> Identities => this.identities;

    /// <summary>Records a physical move.</summary>
    /// <param name="move">The move.</param>
    public void AddMove(RelocationFileMove move)
    {
        ArgumentNullException.ThrowIfNull(move);
        this.moves.Add(move);
    }

    /// <summary>Records an identity move.</summary>
    /// <param name="from">The old identity or identity folder.</param>
    /// <param name="to">The new identity or identity folder.</param>
    /// <param name="isFolder">Whether the identities are folders, which map everything below them.</param>
    public void AddIdentity(AssetPath from, AssetPath to, bool isFolder) => Add(this.identities, from, to, isFolder);

    /// <summary>Records the move of a file's or folder's virtual path.</summary>
    /// <param name="from">The old virtual path.</param>
    /// <param name="to">The new virtual path.</param>
    /// <param name="isFolder">Whether the paths are folders, which map everything below them.</param>
    public void AddPath(AssetPath from, AssetPath to, bool isFolder) => Add(this.paths, from, to, isFolder);

    /// <summary>Maps an asset identity through the relocation.</summary>
    /// <param name="identity">An identity such as <c>/Content/Materials/Red.omat</c>.</param>
    /// <returns>The new identity, or null when the asset does not move.</returns>
    public AssetPath? MapIdentity(AssetPath identity) => Map(this.identities, identity);

    /// <summary>Maps the virtual path of a file or folder through the relocation.</summary>
    /// <param name="path">A virtual path such as <c>/Content/Materials/Red.omat.json</c>.</param>
    /// <returns>The new virtual path, or null when the file or folder does not move.</returns>
    public AssetPath? MapPath(AssetPath path) => Map(this.paths, path);

    /// <summary>Maps a physical location through the moves.</summary>
    /// <param name="path">A location on disk.</param>
    /// <returns>The location after the relocation; the input when it does not move.</returns>
    public FileSystemPath MapPath(FileSystemPath path)
    {
        ArgumentNullException.ThrowIfNull(path);
        foreach (var move in this.moves)
        {
            if (path == move.Source)
            {
                return move.Target;
            }

            if (move.IsDirectory && path.IsSameOrInside(move.Source))
            {
                return move.Target.Combine(path.GetPathBelow(move.Source));
            }
        }

        return path;
    }

    /// <summary>Tests whether a physical location moves, including a change of case only.</summary>
    /// <param name="path">A location on disk.</param>
    /// <returns>Whether the path or one of its folders moves.</returns>
    public bool IsMoved(FileSystemPath path) => !string.Equals(this.MapPath(path).FullPath, path.FullPath, StringComparison.Ordinal);

    private static void Add(List<(AssetPath Source, AssetPath Target, bool IsFolder)> entries, AssetPath from, AssetPath to, bool isFolder)
    {
        ArgumentNullException.ThrowIfNull(from);
        ArgumentNullException.ThrowIfNull(to);
        if (!entries.Exists(entry => entry.Source == from))
        {
            entries.Add((from, to, isFolder));
        }
    }

    private static AssetPath? Map(List<(AssetPath Source, AssetPath Target, bool IsFolder)> entries, AssetPath path)
    {
        ArgumentNullException.ThrowIfNull(path);
        foreach (var (from, to, isFolder) in entries)
        {
            if (path == from)
            {
                return to;
            }

            if (isFolder && path.IsSameOrInside(from))
            {
                return to.Combine(path.GetPathBelow(from));
            }
        }

        return null;
    }
}
