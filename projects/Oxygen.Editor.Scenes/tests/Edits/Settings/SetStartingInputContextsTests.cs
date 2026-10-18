// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Settings;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Scenes.Tests.Edits.Settings;

/// <summary>
/// What is specific to <see cref="SetStartingInputContexts"/> (scene-edits.md 4 and 3.11): the input mapping contexts
/// that are active when the scene starts are replaced as a whole. The edit has no target node. Undo and the change
/// stream of every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class SetStartingInputContextsTests
{
    [TestMethod]
    public async Task Contexts_SetAndRaiseStartingInputContextsChanged()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new SetStartingInputContexts([new StartingInputContextData(SampleScene.PlayInput, 3)])).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Scene.StartingInputContexts.Should().Equal(new StartingInputContextData(SampleScene.PlayInput, 3));
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.StartingInputContextsChanged());
    }

    [TestMethod]
    public async Task RepeatedInputContext_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(
            new SetStartingInputContexts([new StartingInputContextData(SampleScene.PlayInput, 1), new StartingInputContextData(SampleScene.PlayInput, 2)])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryDuplicate);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NullContextEntry_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetStartingInputContexts([null!])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.EntryMissing);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task ContextNotAnInputAsset_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetStartingInputContexts([new StartingInputContextData(SampleScene.MoveScript)])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.AssetReferenceInvalid);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task EquivalentContexts_ApplyWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(
            SampleScene.Builder().StartingInputContexts([new StartingInputContextData(SampleScene.PlayInput, 3)]).Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetStartingInputContexts([new StartingInputContextData(SampleScene.PlayInput, 3)])).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }
}
