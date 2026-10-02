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
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using static Oxygen.Editor.WorldEditor.TestSupport.SceneTestData;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
public sealed partial class SelectionGesturesTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Undo restores each selected target's own value after editing a mixed field.</summary>
    /// <param name="kind">The inspector to exercise.</param>
    /// <param name="field">Its numeric control tag.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Camera", "FieldOfView")]
    [DataRow("Light", "IntensityLux")]
    public Task NumericControlMixedSelectionRestoresEachOriginalValue(string kind, string field) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        var second = AddNumericNode(fixture.Scene, 80);
        SetNumericSource(fixture.Node, kind, 60);
        using var host = fixture.CreateInspectorHost(kind);
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([fixture.Node, second]));
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);
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
        using var fixture = new SceneAuthoringFixture();
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
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() =>
        {
        }).ConfigureAwait(true);
        number.NumberValue = 100;
        RaiseNumberEvent(number, "OnEditSessionCompleted", NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit);
        await PendingNumericEdits(model).ConfigureAwait(true);
        _ = ReadNumericSource(fixture.Node, kind).Should().Be(60, "the original target is cancelled");
        _ = ReadNumericSource(second, kind).Should().Be(80, "the new target rejects the old callback");
        _ = number.NumberValue.Should().Be(80, "the bound control refreshes after rejecting the callback");
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    });

    /// <summary>Relative Transform text edits preserve each target's delta in one undoable command.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task TransformRelativeTextEditAppliesPerTargetAndUndoRestoresOriginalValues() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        var firstTransform = fixture.Node.Components.OfType<TransformComponent>().Single();
        firstTransform.LocalPosition = new(10, 0, 0);
        var second = new SceneNode(fixture.Scene) { Name = "Second" };
        var secondTransform = second.Components.OfType<TransformComponent>().Single();
        secondTransform.LocalPosition = new(20, 0, 0);
        fixture.Scene.RootNodes.Add(second);

        using var host = fixture.CreateInspectorHost("Transform");
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([fixture.Node, second]));
        var model = (TransformViewModel)host.PropertyEditors.Single(editor => MatchesInspector(editor, "Transform"));
        var view = new TransformView { ViewModel = model };
        var scroller = new ScrollViewer { Content = view };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        // Text edit sessions are cancelled when their NumberBox unloads; type only after the editor is visible and stably realized.
        var number = (NumberBox)await FindInspectorControlAsync(
            scroller,
            () => view.FindDescendant<Oxygen.Editor.Controls.PropertyCard>(element => string.Equals(element.PropertyName, "Position", StringComparison.Ordinal))?
                .FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal)),
            "Position.X",
            CancellationToken.None).ConfigureAwait(true);

        await EnterTextAsync(number, "+=2").ConfigureAwait(true);
        number.CompletePendingTextEdit();
        await PendingNumericEdits(model).ConfigureAwait(true);

        _ = firstTransform.LocalPosition.X.Should().Be(12);
        _ = secondTransform.LocalPosition.X.Should().Be(22);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        await fixture.Context.History.UndoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = firstTransform.LocalPosition.X.Should().Be(10);
        _ = secondTransform.LocalPosition.X.Should().Be(20);
        await fixture.Context.History.RedoAsync(CancellationToken.None).ConfigureAwait(true);
        _ = firstTransform.LocalPosition.X.Should().Be(12);
        _ = secondTransform.LocalPosition.X.Should().Be(22);
    });

    /// <summary>Escape cancels a relative Transform input without authoring or dirtying the scene.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task TransformRelativeTextEditEscapeLeavesAllTargetsUnchanged() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        fixture.Node.Components.OfType<TransformComponent>().Single().LocalPosition = new(10, 0, 0);
        var second = new SceneNode(fixture.Scene) { Name = "Second" };
        second.Components.OfType<TransformComponent>().Single().LocalPosition = new(20, 0, 0);
        fixture.Scene.RootNodes.Add(second);

        using var host = fixture.CreateInspectorHost("Transform");
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([fixture.Node, second]));
        var model = (TransformViewModel)host.PropertyEditors.Single(editor => MatchesInspector(editor, "Transform"));
        var view = new TransformView { ViewModel = model };
        var scroller = new ScrollViewer { Content = view };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var number = (NumberBox)await FindInspectorControlAsync(
            scroller,
            () => view.FindDescendant<Oxygen.Editor.Controls.PropertyCard>(element => string.Equals(element.PropertyName, "Position", StringComparison.Ordinal))?
                .FindDescendant<NumberBox>(element => string.Equals(element.Name, "PartNumberBoxX", StringComparison.Ordinal)),
            "Position.X",
            CancellationToken.None).ConfigureAwait(true);

        await EnterTextAsync(number, "+=2").ConfigureAwait(true);
        RaiseNumberEvent(number, "CancelEdit");
        await PendingNumericEdits(model).ConfigureAwait(true);

        _ = fixture.Node.Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(10);
        _ = second.Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(20);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    });
}
