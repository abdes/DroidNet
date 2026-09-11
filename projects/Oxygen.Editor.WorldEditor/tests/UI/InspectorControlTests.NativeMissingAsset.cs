// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks a missing geometry URI without treating accepted dispatch as successful loading.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>A missing geometry reference remains authored and reports the actual scoped native failure.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task MissingGeometryReportsNativeFailureThroughHistoryAndReopen() => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => AddGeometryNode(scene, "Cube"));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        var node = fixture.Source.RootNodes.Single();
        using var host = fixture.CreateInspectorHost([node]);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        var view = new GeometryView { ViewModel = model };
        await LoadTestContentAsync(new ScrollViewer { Content = view }).ConfigureAwait(true);
        var missing = new Uri("asset:///Content/Geometry/DoesNotExist.ogeo");
        _ = (await fixture.Commands.EditGeometryAsync(fixture.Context, [node.Id], new(OptionalEditValues.Supplied<Uri?>(missing)), EditSessionToken.OneShot).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        await AssertMissingGeometryFailureAsync(fixture, node.Id, timeout.Token).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        _ = model.SelectedAssetUriString.Should().Be(missing.ToString());
        _ = view.FindDescendant<SplitButton>(button => string.Equals(button.Name, "AssetSplitButton", StringComparison.Ordinal))!.Content.Should().Be("DoesNotExist.ogeo");
        _ = (await fixture.ReadNodeAsync(node.Id, timeout.Token).ConfigureAwait(true)).GeometryName.Should().Be("cube", "a failed replacement retains the last resolved geometry");
        await fixture.Context.History.UndoAsync(timeout.Token).ConfigureAwait(true);
        _ = await AssertGeometryAsync(fixture, node.Id, "Cube", timeout.Token).ConfigureAwait(true);
        fixture.Results.Clear();
        await fixture.Context.History.RedoAsync(timeout.Token).ConfigureAwait(true);
        await AssertMissingGeometryFailureAsync(fixture, node.Id, timeout.Token).ConfigureAwait(true);
        fixture.Results.Clear();
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        await AssertMissingGeometryFailureAsync(fixture, node.Id, timeout.Token).ConfigureAwait(true);
        _ = fixture.Source.RootNodes.Single().Components.OfType<GeometryComponent>().Single().Geometry!.Uri.Should().Be(missing);
        _ = (await fixture.ReadNodeAsync(node.Id, timeout.Token).ConfigureAwait(true)).GeometryKey.Should().BeEmpty();
    });

    private static async Task AssertMissingGeometryFailureAsync(NativeSceneFixture fixture, Guid nodeId, CancellationToken cancellationToken)
    {
        while (!fixture.Results.Any(result => string.Equals(result.OperationKind, SceneOperationKinds.EditGeometry, StringComparison.Ordinal) && result.Severity == DiagnosticSeverity.Error))
        {
            await Task.Delay(10, cancellationToken).ConfigureAwait(true);
        }

        var failure = fixture.Results.Last(result => string.Equals(result.OperationKind, SceneOperationKinds.EditGeometry, StringComparison.Ordinal));
        _ = failure.Status.Should().Be(OperationStatus.PartiallySucceeded);
        _ = failure.AffectedScope.NodeId.Should().Be(nodeId);
        _ = failure.AffectedScope.DocumentId.Should().Be(fixture.Context.DocumentId);
        _ = failure.AffectedScope.AssetVirtualPath.Should().Contain("DoesNotExist.ogeo");
        _ = failure.Diagnostics.Should().ContainSingle().Which.Code.Should().Be(LiveSyncDiagnosticCodes.GeometryUnresolvedAtRuntime);
        _ = failure.Message.Should().NotBeNullOrEmpty();
    }
}
