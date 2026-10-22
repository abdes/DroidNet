// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Properties;

/// <summary>
/// What is specific to <see cref="SetProperties"/> (scene-edits.md 3.10). The value and cross-field rules of each
/// component kind are the <c>PropertyRules</c> classes; targets, locks, undo and the change stream of every edit are
/// <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class SetPropertiesTests
{
    private static NodeId SecondCam { get; } = NodeId.FromValue(SampleScene.Tagged(1, "CB"));

    [TestMethod]
    public async Task OneNode_WritesTheValuesAndRaisesCommit()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(SetX((SampleScene.A, 3f))).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = PositionX(fixture, SampleScene.A).Should().Be(3f);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.PropertiesChanged(SampleScene.A, [SceneSchema.Transform.PositionX.Id], EditPhase.Commit));
    }

    [TestMethod]
    public async Task TwoNodes_RecordOneEntryAndRaiseOneChangeEach()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(SetX((SampleScene.A, 3f), (SampleScene.B, 4f))).ConfigureAwait(false);

        // The order of the nodes' changes is not part of the contract.
        SceneAssert.Applied(result);
        _ = fixture.History.UndoStack.Should().ContainSingle();
        _ = fixture.Changes.Only().Should().BeEquivalentTo(
            new SceneChange[]
            {
                new SceneChange.PropertiesChanged(SampleScene.A, [SceneSchema.Transform.PositionX.Id], EditPhase.Commit),
                new SceneChange.PropertiesChanged(SampleScene.B, [SceneSchema.Transform.PositionX.Id], EditPhase.Commit),
            },
            static options => options.PreferringRuntimeMemberTypes());
    }

    [TestMethod]
    public async Task Undo_RestoresTheBeforeValuesOfEachNode()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var beforeA = PositionX(fixture, SampleScene.A);
        var beforeB = PositionX(fixture, SampleScene.B);
        var result = await fixture.Authoring.ExecuteAsync(SetX((SampleScene.A, 3f), (SampleScene.B, 4f))).ConfigureAwait(false);

        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Applied(undone);
        _ = PositionX(fixture, SampleScene.A).Should().Be(beforeA);
        _ = PositionX(fixture, SampleScene.B).Should().Be(beforeB);
    }

    [TestMethod]
    public async Task ValuesEqualTheCurrent_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(SetX((SampleScene.A, PositionX(fixture, SampleScene.A)))).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    // The catalogue words this "camera property on A", which is the case of NodeWithoutTheComponent; the property of
    // another kind here is one that no node component has.
    [TestMethod]
    public async Task PropertyOfAnotherKind_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var edit = new PropertyEdit.Builder().Set(SceneProperties.SceneEnvironment.ExposureCompensation, 1f).Build();
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(Edit((SampleScene.A, edit))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertyUnknown);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task MixedComponentKinds_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var edit = new PropertyEdit.Builder().Set(SceneSchema.Transform.PositionX, 3f).Set(SceneSchema.PerspectiveCamera.NearPlane, 0.2f).Build();
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(Edit((SampleScene.Cam, edit))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertyUnknown);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NodeWithoutTheComponent_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var edit = new PropertyEdit.Builder().Set(SceneSchema.PerspectiveCamera.NearPlane, 0.2f).Build();
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(Edit((SampleScene.B, edit))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task OneNodeBreaksARule_ChangesNoNode()
    {
        var scene = SampleScene.Builder()
            .Node(
                SecondCam,
                "Cam2",
                components: [new PerspectiveCameraData { Id = ComponentId.FromValue(SampleScene.Tagged(2, "CB")), Name = "Camera", NearPlane = 0.1f, FarPlane = 5f, AspectMode = CameraAspectMode.Auto }])
            .Build();
        using var fixture = await AuthoringFixture.CreateAsync(scene).ConfigureAwait(false);
        var near = new PropertyEdit.Builder().Set(SceneSchema.PerspectiveCamera.NearPlane, 10f).Build();
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(Edit((SampleScene.Cam, near), (SecondCam, near))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertiesInconsistent, SecondCam);
        _ = fixture.Node(SampleScene.Cam).Components.OfType<PerspectiveCamera>().Single().NearPlane.Should().Be(0.1f);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task EmptyEdits_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(Edit()).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.NothingToEdit);
        SceneAssert.Unchanged(fixture, before);
    }

    private static float PositionX(AuthoringFixture fixture, NodeId node)
        => fixture.Node(node).Transform.LocalPosition.X;

    private static SetProperties Edit(params (NodeId Node, PropertyEdit Edit)[] edits)
        => new(edits.ToDictionary(static pair => pair.Node, static pair => pair.Edit), "Edit");

    private static SetProperties SetX(params (NodeId Node, float Value)[] values)
        => Edit([.. values.Select(static pair => (pair.Node, new PropertyEdit.Builder().Set(SceneSchema.Transform.PositionX, pair.Value).Build()))]);
}
