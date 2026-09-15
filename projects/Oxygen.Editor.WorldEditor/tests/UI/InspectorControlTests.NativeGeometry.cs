// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises asset picker assignments against resolved native geometry.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Every engine built-in resolves with drawable buffers and retains its URI across Save/reopen.</summary>
    /// <param name="shape">The canonical built-in generator name.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Cube")]
    [DataRow("SubdividedCube")]
    [DataRow("Sphere")]
    [DataRow("IcoSphere")]
    [DataRow("Capsule")]
    [DataRow("Plane")]
    [DataRow("Cylinder")]
    [DataRow("Cone")]
    [DataRow("Torus")]
    [DataRow("Quad")]
    public Task BuiltinGeometryResolvesAndReopensThroughNativeRuntime(string shape) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => AddGeometryNode(scene, shape));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        var nodeId = fixture.Source.RootNodes.Single().Id;
        var original = await AssertGeometryAsync(fixture, nodeId, shape, timeout.Token).ConfigureAwait(true);

        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        var reopened = await AssertGeometryAsync(fixture, nodeId, shape, timeout.Token).ConfigureAwait(true);

        _ = reopened.GeometryKey.Should().Be(original.GeometryKey);
        _ = reopened.VertexCount.Should().Be(original.VertexCount);
        _ = reopened.IndexCount.Should().Be(original.IndexCount);
    });

    /// <summary>A mixed geometry selection converges through the picker, history and saved source.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task GeometryPickerMixedSelectionHistoryAndReopenReachNativeState() => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene =>
        {
            AddGeometryNode(scene, "Cube");
            AddGeometryNode(scene, "Plane");
        });
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        VisualUserInterfaceTestsApp.MainWindow.Activate();
        var nodes = fixture.Source.RootNodes.Select(node => node.Id).ToArray();
        using var host = fixture.CreateInspectorHost(fixture.Source.RootNodes.ToList());
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        var view = new GeometryView { ViewModel = model };
        var scroller = new ScrollViewer { Content = view, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        var button = (SplitButton)await FindInspectorControlAsync(scroller, () => view.FindDescendant<SplitButton>(value => string.Equals(value.Name, "AssetSplitButton", StringComparison.Ordinal)), "Geometry", timeout.Token).ConfigureAwait(true);
        _ = button.Content.Should().Be("--");
        await PickAssetAsync(button, "Sphere", material: false, timeout.Token).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        _ = await AssertGeometryAsync(fixture, nodes[0], "Sphere", timeout.Token).ConfigureAwait(true);
        _ = await AssertGeometryAsync(fixture, nodes[1], "Sphere", timeout.Token).ConfigureAwait(true);
        _ = button.Content.Should().Be("Sphere");
        await fixture.Context.History.UndoAsync(timeout.Token).ConfigureAwait(true);
        _ = await AssertGeometryAsync(fixture, nodes[0], "Cube", timeout.Token).ConfigureAwait(true);
        _ = await AssertGeometryAsync(fixture, nodes[1], "Plane", timeout.Token).ConfigureAwait(true);
        _ = button.Content.Should().Be("--");
        await fixture.Context.History.RedoAsync(timeout.Token).ConfigureAwait(true);
        _ = await AssertGeometryAsync(fixture, nodes[0], "Sphere", timeout.Token).ConfigureAwait(true);
        _ = await AssertGeometryAsync(fixture, nodes[1], "Sphere", timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        _ = await AssertGeometryAsync(fixture, nodes[0], "Sphere", timeout.Token).ConfigureAwait(true);
        _ = await AssertGeometryAsync(fixture, nodes[1], "Sphere", timeout.Token).ConfigureAwait(true);
    });

    private static void AddGeometryNode(Scene scene, string shape)
    {
        var node = new SceneNode(scene) { Name = shape };
        _ = node.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri($"BasicShapes/{shape}")) });
        scene.RootNodes.Add(node);
    }

    private static async Task<RuntimeNodeState> AssertGeometryAsync(NativeSceneFixture fixture, Guid nodeId, string shape, CancellationToken cancellationToken)
    {
        var geometry = fixture.Source.RootNodes.Single(node => node.Id == nodeId).Components.OfType<GeometryComponent>().Single();
        _ = geometry.Geometry.Should().NotBeNull();
        _ = geometry.Geometry!.Uri.Should().Be(AssetUris.BuildGeneratedUri($"BasicShapes/{shape}"));
        var native = await WaitForNodeAsync(fixture, nodeId, state => string.Equals(state.GeometryName, shape, StringComparison.OrdinalIgnoreCase), cancellationToken).ConfigureAwait(true);
        _ = native.GeometryKey.Should().NotBeNullOrEmpty();
        _ = native.VertexCount.Should().BePositive();
        _ = native.IndexCount.Should().BePositive();
        return native;
    }

    private static async Task<RuntimeNodeState> WaitForNodeAsync(NativeSceneFixture fixture, Guid nodeId, Func<RuntimeNodeState, bool> expected, CancellationToken cancellationToken)
    {
        RuntimeNodeState? state = null;
        for (var attempt = 0; attempt < 150; attempt++)
        {
            state = await fixture.ReadNodeAsync(nodeId, cancellationToken).ConfigureAwait(true);
            if (expected(state))
            {
                return state;
            }

            await Task.Delay(10, cancellationToken).ConfigureAwait(true);
        }

        _ = expected(state!).Should().BeTrue("native state must converge: {0}; materials: {1}; outcomes: {2}", state, string.Join(", ", state!.MaterialKeys), string.Join("; ", fixture.Results.Select(result => result.Message)));
        return state;
    }

    private static async Task PickAssetAsync(SplitButton owner, string name, bool material, CancellationToken cancellationToken)
    {
        var flyout = (Flyout)owner.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        var closed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnClosed(object? sender, object args) => closed.TrySetResult();
        flyout.Closed += OnClosed;
        try
        {
            flyout.ShowAt(owner);
            Button? choice = null;
            while (choice is null)
            {
                _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
                cancellationToken.ThrowIfCancellationRequested();
                choice = flyout.Content.FindDescendant<Button>(button => material
                    ? button.DataContext is MaterialPickerRow row && string.Equals(row.Item.Name, name, StringComparison.Ordinal)
                    : button.DataContext is AssetPickerRow asset && string.Equals(asset.Item.Name, name, StringComparison.Ordinal));
            }

            _ = choice.IsEnabled.Should().BeTrue();
            ((IInvokeProvider)new ButtonAutomationPeer(choice).GetPattern(PatternInterface.Invoke)).Invoke();
            await closed.Task.WaitAsync(cancellationToken).ConfigureAwait(true);
        }
        finally
        {
            flyout.Closed -= OnClosed;
            flyout.Hide();
        }
    }
}
