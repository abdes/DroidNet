// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits;

/// <summary>
/// One row of <see cref="EditSamples"/>: an edit of one type over <c>SampleScene</c>, and what the contract says it
/// raises.
/// </summary>
/// <param name="Name">The edit type's name, or <c>Batch</c> / <c>NestedBatch</c>; the row's display name.</param>
/// <param name="Build">Builds the edit from the sample scene's data.</param>
/// <param name="Expected">The changes executing the edit raises (scene-edits.md 3.7).</param>
/// <param name="ExpectedUndo">The changes undoing the edit raises: those of the revert named in the edit catalogue.</param>
public sealed record EditSample(
    string Name,
    Func<SceneData, ISceneEdit> Build,
    IReadOnlyList<SceneChange> Expected,
    IReadOnlyList<SceneChange> ExpectedUndo)
{
    /// <summary>Gets a value indicating whether the edit's <see cref="ISceneEdit.Targets"/> are not empty.</summary>
    public bool HasTargets { get; init; }

    /// <summary>Gets a value indicating whether the edit writes an asset reference (scene-edits.md 3.11).</summary>
    public bool CarriesAssetReference { get; init; }

    /// <summary>Gets the asset reference the edit writes; set when <see cref="CarriesAssetReference"/> is true.</summary>
    public AssetPath? Asset { get; init; }

    /// <summary>Gets the node that holds <see cref="Asset"/>, or <see langword="null"/> for the scene's own references.</summary>
    public NodeId? AssetNode { get; init; }

    /// <summary>
    /// Gets a value indicating whether the order of <see cref="Expected"/> is part of the contract. It is for
    /// batches, whose changes are the inner changes in order (3.5); an edit's own changes have no stated order.
    /// </summary>
    public bool ChangesInOrder { get; init; }

    /// <summary>
    /// Gets how to build the edit from inputs the test can change afterwards, or <see langword="null"/> when the edit
    /// is not built from serialization records or property edits.
    /// </summary>
    public Func<SceneData, TamperedEdit>? Tamper { get; init; }

    /// <summary>
    /// Gets what the edit needs of the fixture beyond the sample scene, such as a source's structure that makes one of its
    /// changes a kept change, or <see langword="null"/> when it needs nothing.
    /// </summary>
    public Action<AuthoringFixture>? Arrange { get; init; }

    /// <inheritdoc/>
    public override string ToString() => this.Name;
}
