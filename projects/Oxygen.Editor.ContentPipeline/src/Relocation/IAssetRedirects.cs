// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// The session's renames and moves, so that references captured earlier (undo and redo steps, copied nodes)
/// resolve to the asset's current identity. History is never rewritten; references resolve when restored.
/// </summary>
public interface IAssetRedirects
{
    /// <summary>Resolves a captured asset reference to the asset's current identity, keeping its form.</summary>
    /// <param name="reference">An asset URI or a canonical virtual path.</param>
    /// <returns>The current reference; the input when the asset did not move or still exists at that path.</returns>
    public string Resolve(string reference);

    /// <summary>Resolves a captured asset to its current identity.</summary>
    /// <param name="reference">The asset's identity when it was captured.</param>
    /// <returns>The current identity; the input when the asset did not move or still exists at that path.</returns>
    public AssetPath Resolve(AssetPath reference);

    /// <summary>Tests whether a reference names an asset this session deleted and nothing replaced.</summary>
    /// <param name="reference">The asset's identity.</param>
    /// <returns>Whether restoring the reference restores a missing reference.</returns>
    public bool WasDeleted(AssetPath reference);
}
