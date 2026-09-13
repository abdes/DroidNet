// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentBrowser.AssetIdentity;

namespace Oxygen.Editor.World.Services;

/// <summary>Owns saved-asset preview requests for the active scene and accepted picker assignments.</summary>
public interface ISceneContentDemandService
{
    /// <summary>Requests the saved asset after its undoable assignment has been accepted.</summary>
    /// <param name="scene">The consuming authoring scene, which need not be saved.</param>
    /// <param name="nodeIds">The selected assignment targets.</param>
    /// <param name="assetUri">The assigned identity; null means no material override.</param>
    /// <param name="kind">The geometry or material slot being assigned.</param>
    public void RequestAssignment(Scene scene, IReadOnlyList<Guid> nodeIds, Uri? assetUri, AssetKind kind);
}
