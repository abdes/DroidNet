// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.World.Tests;

/// <summary>Verifies captured numeric targets and inline feedback across inspector selection changes.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Undo restores each selected target's own value after editing a mixed field.</summary>
    /// <param name="kind">The inspector to exercise.</param>
    /// <param name="field">Its numeric control tag.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Camera", "FieldOfView")]
    [DataRow("Light", "IntensityLux")]
    public Task NumericControlMixedSelectionRestoresEachOriginalValue(string kind, string field) => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        var second = AddNumericNode(fixture.Scene, 80);
        SetNumericSource(fixture.Node, kind, 60);
        using var host = fixture.CreateInspectorHost(kind);
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([fixture.Node, second]));
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var model = host.PropertyEditors.Single(editor => MatchesInspector(editor, kind));
        var view = CreateNumericView(model);
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(element => Equals(element.Tag, field))!;
        RaiseNumberEvent(number, "OnEditSessionStarted", NumberBoxEditInteractionKind.PointerDrag);
        number.NumberValue = 90;
        RaiseNumberEvent(number, "OnEditSessionCompleted", NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit);
        await PendingNumericEdits(model).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        _ = ReadNumericSource(fixture.Node, kind).Should().Be(90);
        _ = ReadNumericSource(second, kind).Should().Be(90);
        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = ReadNumericSource(fixture.Node, kind).Should().Be(60);
        _ = ReadNumericSource(second, kind).Should().Be(80);
        await fixture.Context.History.RedoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = ReadNumericSource(fixture.Node, kind).Should().Be(90);
        _ = ReadNumericSource(second, kind).Should().Be(90);
    });

    /// <summary>A selection change cancels the old pointer session and ignores its late value and terminal callback.</summary>
    /// <param name="kind">The inspector to exercise.</param>
    /// <param name="field">Its numeric control tag.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Camera", "FieldOfView")]
    [DataRow("Light", "IntensityLux")]
    public Task NumericControlLateSelectionCallbackCannotEditTheNewTarget(string kind, string field) => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        var second = AddNumericNode(fixture.Scene, 80);
        SetNumericSource(fixture.Node, kind, 60);
        using var host = fixture.CreateInspectorHost(kind);
        var model = host.PropertyEditors.Single(editor => MatchesInspector(editor, kind));
        var view = CreateNumericView(model);
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(element => Equals(element.Tag, field))!;
        RaiseNumberEvent(number, "OnEditSessionStarted", NumberBoxEditInteractionKind.PointerDrag);
        number.NumberValue = 90;
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([second]));
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        number.NumberValue = 100;
        RaiseNumberEvent(number, "OnEditSessionCompleted", NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit);
        await PendingNumericEdits(model).ConfigureAwait(true);
        _ = ReadNumericSource(fixture.Node, kind).Should().Be(60, "the original target is cancelled");
        _ = ReadNumericSource(second, kind).Should().Be(80, "the new target rejects the old callback");
        _ = number.NumberValue.Should().Be(80, "the bound control refreshes after rejecting the callback");
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    });

    private static SceneNode AddNumericNode(Scene scene, float value)
    {
        var node = new SceneNode(scene) { Name = "Other target" };
        _ = node.AddComponent(new PerspectiveCamera { Name = "Camera", FieldOfView = value });
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Light", IntensityLux = value });
        scene.RootNodes.Add(node);
        return node;
    }

    private static float ReadNumericSource(SceneNode node, string kind)
        => string.Equals(kind, "Camera", StringComparison.Ordinal)
            ? node.Components.OfType<PerspectiveCamera>().Single().FieldOfView
            : node.Components.OfType<DirectionalLightComponent>().Single().IntensityLux;

    private static void SetNumericSource(SceneNode node, string kind, float value)
    {
        if (string.Equals(kind, "Camera", StringComparison.Ordinal))
        {
            node.Components.OfType<PerspectiveCamera>().Single().FieldOfView = value;
        }
        else
        {
            node.Components.OfType<DirectionalLightComponent>().Single().IntensityLux = value;
        }
    }
}
