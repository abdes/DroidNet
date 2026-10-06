// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Checks scene reference validation, history, and dirty-state behavior.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task EditSceneReferencesAsync_UndoRedoRestoresTheWholeReferenceSet()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var context = CreateContext(scene);
        var references = new SceneReferencesData
        {
            Scripts = [new("asset:///Content/Scripts/Orbit.oscript.json")],
            InputActions = [new("asset:///Content/Input/Jump.oiact")],
            InputMappingContexts = [new("asset:///Library/Input/Gameplay.oimap")],
            PhysicsSidecars = [new("asset:///Content/Physics/Main.opscene.json")],
            ExtraAssets = ["/.cooked/Extras/lookup.bin"],
        };

        var result = await fixture.Sut.EditSceneReferencesAsync(context, references).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.References.Should().BeEquivalentTo(references);
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.References.IsEmpty.Should().BeTrue();
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.References.Should().BeEquivalentTo(references);
    }

    [TestMethod]
    [DataRow("asset:///Content/Scripts/NotAMaterial.omat.json", "/.cooked/Extras/lookup.bin")]
    [DataRow("asset:///Content/Scripts/Orbit.oscript.json", "/.cooked/Extras/../escape.bin")]
    public async Task EditSceneReferencesAsync_InvalidReferenceIsRejectedWithoutMutation(string scriptUri, string extraAsset)
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var context = CreateContext(scene);
        var before = scene.References;

        var result = await fixture.Sut.EditSceneReferencesAsync(context, new SceneReferencesData
        {
            Scripts = [new(scriptUri)],
            ExtraAssets = [extraAsset],
        }).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = scene.References.Should().BeSameAs(before);
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = result.ValidationCode.Should().Be("SCENE_REFERENCES_INVALID");
    }
}
