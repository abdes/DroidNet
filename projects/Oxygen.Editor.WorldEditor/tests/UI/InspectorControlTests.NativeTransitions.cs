// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;

namespace Oxygen.Editor.World.Tests;

/// <summary>Qualifies component lifecycle and locked Transform behavior against a native scene.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Component add/remove history and saved reload preserve the native component set.</summary>
    /// <param name="camera">Whether to exercise a camera or a directional light.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public Task ComponentTransitionsAndLockedTransformReachNativeState(bool camera) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => SeedNativeNode(scene, "Transform"));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        var node = fixture.Source.RootNodes.Single();
        using var host = fixture.CreateInspectorHost([node]);
        var view = new SceneNodeDetailsView { Node = node, HistoryRoot = host };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await AssertTransformRemovalDeniedAsync(fixture, view, timeout.Token).ConfigureAwait(true);
        var type = camera ? typeof(PerspectiveCamera) : typeof(DirectionalLightComponent);
        var added = await fixture.Commands.AddComponentAsync(fixture.Context, node.Id, type).ConfigureAwait(true);
        _ = added.Succeeded.Should().BeTrue();
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        await AssertComponentPresenceAsync(fixture, host, node.Id, camera, present: true, timeout.Token).ConfigureAwait(true);
        await fixture.Context.History.UndoAsync(timeout.Token).ConfigureAwait(true);
        await AssertComponentPresenceAsync(fixture, host, node.Id, camera, present: false, timeout.Token).ConfigureAwait(true);
        await fixture.Context.History.RedoAsync(timeout.Token).ConfigureAwait(true);
        await AssertComponentPresenceAsync(fixture, host, node.Id, camera, present: true, timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        await AssertComponentPresenceAsync(fixture, host, node.Id, camera, present: true, timeout.Token).ConfigureAwait(true);
        var reopened = fixture.Source.RootNodes.Single();
        var component = reopened.Components.Single(value => value.GetType() == type);
        _ = (await fixture.Commands.RemoveComponentAsync(fixture.Context, reopened.Id, component.Id).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        await AssertComponentPresenceAsync(fixture, host, node.Id, camera, present: false, timeout.Token).ConfigureAwait(true);
        await fixture.Context.History.UndoAsync(timeout.Token).ConfigureAwait(true);
        await AssertComponentPresenceAsync(fixture, host, node.Id, camera, present: true, timeout.Token).ConfigureAwait(true);
        await fixture.Context.History.RedoAsync(timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        await AssertComponentPresenceAsync(fixture, host, node.Id, camera, present: false, timeout.Token).ConfigureAwait(true);
        view.Node = null;
    });

    private static async Task AssertTransformRemovalDeniedAsync(NativeSceneFixture fixture, SceneNodeDetailsView view, CancellationToken cancellationToken)
    {
        var node = fixture.Source.RootNodes.Single();
        var transform = node.Components.OfType<TransformComponent>().Single();
        view.ViewModel!.SelectedComponent = transform;
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var delete = view.FindDescendants().OfType<Button>().Single(button => button.Content is FontIcon { Glyph: "\uE74D" });
        _ = delete.IsEnabled.Should().BeFalse();
        _ = (await fixture.Commands.RemoveComponentAsync(fixture.Context, node.Id, transform.Id).ConfigureAwait(true)).Succeeded.Should().BeFalse();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = (await fixture.ReadNodeAsync(node.Id, cancellationToken).ConfigureAwait(true)).Properties.Should().HaveCount(9);
    }

    private static async Task AssertComponentPresenceAsync(NativeSceneFixture fixture, SceneNodeEditorViewModel host, Guid nodeId, bool camera, bool present, CancellationToken cancellationToken)
    {
        var componentId = camera ? 2 : 3;
        var native = await fixture.ReadNodeAsync(nodeId, cancellationToken).ConfigureAwait(true);
        _ = native.Properties.Count(value => value.ComponentId == componentId).Should().Be(present ? camera ? 4 : 25 : 0);
        _ = host.PropertyEditors.Any(editor => camera ? editor is PerspectiveCameraViewModel : editor is DirectionalLightViewModel).Should().Be(present);
        _ = fixture.Source.RootNodes.Single(node => node.Id == nodeId).Components.Any(component => camera ? component is PerspectiveCamera : component is DirectionalLightComponent).Should().Be(present);
    }
}
