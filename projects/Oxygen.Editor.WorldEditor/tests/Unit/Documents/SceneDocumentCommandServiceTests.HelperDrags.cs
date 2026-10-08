// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>
/// A light helper's handle drag runs the gizmo's transaction: previews in one gesture, one undo
/// entry on release and the exact starting value on cancel.
/// </summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task HelperDrag_CommitsOneUndoEntryWithTheLastValue()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var (node, light) = SpotNode(scene);
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        var session = EditSessionToken.Begin([node.Id], "Viewport.Helper");

        _ = await DragOuterConeAsync(fixture, context, node, session, 0.6f).ConfigureAwait(false);
        var last = await DragOuterConeAsync(fixture, context, node, session, 0.7f).ConfigureAwait(false);
        session.Commit();
        _ = await fixture.Sut.EditPropertiesForTargetsAsync(context, last, "Outer cone angle", session).ConfigureAwait(false);

        _ = light.OuterConeAngleRadians.Should().Be(0.7f);
        _ = context.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task HelperDrag_CancelRestoresTheStartingValueWithoutHistory()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var (node, light) = SpotNode(scene);
        var context = CreateContext(scene);
        _ = ConfigureGestureSync(fixture, scene);
        var session = EditSessionToken.Begin([node.Id], "Viewport.Helper");

        var last = await DragOuterConeAsync(fixture, context, node, session, 0.7f).ConfigureAwait(false);
        _ = light.OuterConeAngleRadians.Should().Be(0.7f);
        session.Cancel();
        _ = await fixture.Sut.EditPropertiesForTargetsAsync(context, last, "Outer cone angle", session).ConfigureAwait(false);

        _ = light.OuterConeAngleRadians.Should().Be(0.5f);
        _ = context.History.UndoStack.Should().BeEmpty();
    }

    private static (SceneNode Node, SpotLightComponent Light) SpotNode(Scene scene)
    {
        var node = new SceneNode(scene) { Name = "Spot" };
        scene.RootNodes.Add(node);
        var light = new SpotLightComponent { Name = "Spot", Range = 10f, InnerConeAngleRadians = 0.3f, OuterConeAngleRadians = 0.5f };
        _ = node.AddComponent(light);
        return (node, light);
    }

    private static async Task<Dictionary<Guid, PropertyEdit>> DragOuterConeAsync(
        Fixture fixture,
        SceneDocumentCommandContext context,
        SceneNode node,
        EditSessionToken session,
        float radians)
    {
        var edits = new Dictionary<Guid, PropertyEdit>
        {
            [node.Id] = SceneEditorViewModel.BuildHelperEdit(node, RuntimeHelperHandle.OuterCone, radians)!,
        };
        _ = await fixture.Sut.EditPropertiesForTargetsAsync(context, edits, "Outer cone angle", session).ConfigureAwait(false);
        return edits;
    }
}
