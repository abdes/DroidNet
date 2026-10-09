// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// The session's renames and moves, so that references captured earlier (undo and redo steps, copied nodes)
/// resolve to the asset's current identity. History is never rewritten; references resolve when restored.
/// </summary>
public interface IAssetRedirects
{
    /// <summary>Resolves a captured asset reference to the asset's current identity, keeping its form.</summary>
    /// <param name="reference">A full <c>asset:///</c> URI or a canonical virtual path.</param>
    /// <returns>The current reference; the input when the asset did not move or still exists at that path.</returns>
    public string Resolve(string reference);

    /// <summary>Resolves a captured asset URI to the asset's current identity.</summary>
    /// <param name="reference">The asset URI.</param>
    /// <returns>The current URI; the input when the asset did not move or still exists at that path.</returns>
    public Uri Resolve(Uri reference);

    /// <summary>Tests whether a reference names an asset this session deleted and nothing replaced.</summary>
    /// <param name="reference">The asset URI.</param>
    /// <returns>Whether restoring the reference restores a missing reference.</returns>
    public bool WasDeleted(Uri reference);
}
