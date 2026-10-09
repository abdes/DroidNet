// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// The physical moves of a relocation and the identity mapping they imply. Identity moves also cover
/// imported output groups, which have no folders on disk.
/// </summary>
internal sealed class RelocationMap
{
    private readonly List<(string source, string target, bool isFolder)> identities = [];
    private readonly List<RelocationFileMove> moves = [];

    /// <summary>Gets the physical moves in the order they apply.</summary>
    public IReadOnlyList<RelocationFileMove> Moves => this.moves;

    /// <summary>Gets the identity moves: files map exactly, folders map by prefix.</summary>
    public IEnumerable<(string source, string target, bool isFolder)> Identities => this.identities;

    /// <summary>Records a physical move.</summary>
    /// <param name="move">The move.</param>
    public void AddMove(RelocationFileMove move)
    {
        ArgumentNullException.ThrowIfNull(move);
        this.moves.Add(move with { Source = Path.GetFullPath(move.Source), Target = Path.GetFullPath(move.Target) });
    }

    /// <summary>Records an identity move.</summary>
    /// <param name="from">The old identity or identity folder.</param>
    /// <param name="to">The new identity or identity folder.</param>
    /// <param name="isFolder">Whether the identities are folders, mapped by prefix.</param>
    public void AddIdentity(string from, string to, bool isFolder)
    {
        if (!this.identities.Exists(entry => string.Equals(entry.source, from, StringComparison.OrdinalIgnoreCase)))
        {
            this.identities.Add((from, to, isFolder));
        }
    }

    /// <summary>Maps an asset identity through the relocation.</summary>
    /// <param name="identity">An identity such as <c>/Content/Materials/Red.omat</c>.</param>
    /// <returns>The new identity, or null when the asset does not move.</returns>
    public string? MapIdentity(string identity)
    {
        foreach (var (from, to, isFolder) in this.identities)
        {
            if (string.Equals(identity, from, StringComparison.OrdinalIgnoreCase))
            {
                return to;
            }

            if (isFolder && identity.StartsWith(from + "/", StringComparison.OrdinalIgnoreCase))
            {
                return to + identity[from.Length..];
            }
        }

        return null;
    }

    /// <summary>Maps a physical location through the moves.</summary>
    /// <param name="path">An absolute path.</param>
    /// <returns>The location after the relocation; the input when it does not move.</returns>
    public string MapPath(string path)
    {
        var full = Path.GetFullPath(path);
        foreach (var move in this.moves)
        {
            if (string.Equals(full, move.Source, StringComparison.OrdinalIgnoreCase))
            {
                return move.Target;
            }

            if (move.IsDirectory && full.StartsWith(move.Source + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            {
                return move.Target + full[move.Source.Length..];
            }
        }

        return full;
    }

    /// <summary>Tests whether a physical location moves.</summary>
    /// <param name="path">An absolute path.</param>
    /// <returns>Whether the path or one of its folders moves.</returns>
    public bool IsMoved(string path) => !string.Equals(this.MapPath(path), Path.GetFullPath(path), StringComparison.Ordinal);
}
