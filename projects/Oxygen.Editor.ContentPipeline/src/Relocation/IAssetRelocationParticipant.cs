// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// An open document's owner, kept in step with relocations without reloading. The relocation runs only while no
/// document has unsaved changes, so an owner's document equals its file before the change.
/// </summary>
public interface IAssetRelocationParticipant
{
    /// <summary>
    /// Re-points the document's references, and its own location when it moved, in memory and outside undo history,
    /// and records the rewritten file's version as its saved state. Called after the files are committed and before
    /// the follow-up cook, which must see a consistent document. Called on a background thread.
    /// </summary>
    /// <param name="change">The committed relocation.</param>
    /// <returns>Completion after the document follows the change.</returns>
    public Task FollowAsync(AssetRelocationChange change);
}
