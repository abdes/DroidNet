// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

/// <summary>
/// Command-driven structural mutations must reach the demand service's direct reference observers.
/// Each test dirties the document metadata up front so the invalidation under assertion cannot come
/// from the first-dirty metadata transition, only from the observed collection graph.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "partial class with public part")]
public sealed partial class ContentDemandTests
{
    /// <summary>Deleting the referencing node through the document command retires the activation demand.</summary>
    /// <returns>The asynchronous command-driven delete regression.</returns>
    [TestMethod]
    public Task CommandDrivenDeleteRetiresSceneDemandWithoutMetadataTransition() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        fixture.Activate();
        _ = fixture.Requests.Should().ContainSingle();
        ConfigureStructuralSync(fixture);
        fixture.Authoring.Context.Metadata.IsDirty = true;

        _ = (await fixture.Authoring.Commands.DeleteNodesAsync(fixture.Authoring.Context, [fixture.Authoring.Node.Id]).ConfigureAwait(true)).Succeeded.Should().BeTrue();

        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue("the command-driven delete must retire the activation demand through the reference observers while the document is already dirty");
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Requests.Should().ContainSingle();
    });

    /// <summary>Removing the geometry component through the document command retires its demand.</summary>
    /// <returns>The asynchronous command-driven component removal regression.</returns>
    [TestMethod]
    public Task CommandDrivenComponentRemovalRetiresSceneDemand() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        fixture.Activate();
        _ = fixture.Requests.Should().ContainSingle();
        ConfigureStructuralSync(fixture);
        fixture.Authoring.Context.Metadata.IsDirty = true;

        _ = (await fixture.Authoring.Commands.RemoveComponentAsync(fixture.Authoring.Context, fixture.Authoring.Node.Id, fixture.Geometry.Id).ConfigureAwait(true)).Succeeded.Should().BeTrue();

        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue("the command-driven component removal must retire the geometry demand through the Components observer");
        await WaitForRenderAsync().ConfigureAwait(true);
    });

    /// <summary>A node and component created through commands are re-subscribed, so a later removal retires the targeted demand.</summary>
    /// <returns>The asynchronous command-created re-subscription regression.</returns>
    [TestMethod]
    public Task CommandCreatedNodeAndComponentAreResubscribedAndRetireTargetedDemand() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Activate();
        ConfigureStructuralSync(fixture);
        fixture.Authoring.Context.Metadata.IsDirty = true;
        var context = fixture.Authoring.Context;

        var created = await fixture.Authoring.Commands.CreateNodeAsync(context, parentNodeId: null, parentFolderId: null, "Created").ConfigureAwait(true);
        _ = created.Succeeded.Should().BeTrue();
        var component = await fixture.Authoring.Commands.AddComponentAsync(context, created.Value!.Id, typeof(GeometryComponent)).ConfigureAwait(true);
        _ = component.Succeeded.Should().BeTrue();
        _ = (await fixture.Authoring.Commands.EditGeometryAsync(context, [created.Value!.Id], new GeometryEdit(OptionalEditValues.Supplied<Uri?>(fixture.GeometryUri)), EditSessionToken.OneShot).ConfigureAwait(true)).Succeeded.Should().BeTrue();

        var selection = new object();
        fixture.Authoring.Messenger.Register<SceneNodeSelectionRequestMessage>(selection, (_, message) => message.Reply([created.Value!]));
        fixture.Service.RequestAssignment(fixture.Authoring.Scene, [created.Value!.Id], fixture.GeometryUri, AssetKind.Geometry);
        _ = fixture.Requests.Should().ContainSingle().Which.uri.Should().Be(fixture.GeometryUri);

        _ = (await fixture.Authoring.Commands.RemoveComponentAsync(context, created.Value!.Id, component.Value!.Id).ConfigureAwait(true)).Succeeded.Should().BeTrue();

        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue("the service must have re-subscribed the command-created node's component collections for this removal to retire the demand");
        await WaitForRenderAsync().ConfigureAwait(true);
    });

    /// <summary>A command-driven reparent keeps the live demand current, and deleting the new parent retires it.</summary>
    /// <returns>The asynchronous command-driven reparent regression.</returns>
    [TestMethod]
    public Task CommandDrivenReparentKeepsDemandUntilItsReferenceIsDeleted() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        fixture.Activate();
        _ = fixture.Requests.Should().ContainSingle();
        ConfigureStructuralSync(fixture);
        fixture.Authoring.Context.Metadata.IsDirty = true;
        var context = fixture.Authoring.Context;

        var parent = await fixture.Authoring.Commands.CreateNodeAsync(context, parentNodeId: null, parentFolderId: null, "Parent").ConfigureAwait(true);
        _ = parent.Succeeded.Should().BeTrue();
        _ = (await fixture.Authoring.Commands.ReparentNodesAsync(context, [fixture.Authoring.Node.Id], parent.Value!.Id, preserveWorldTransform: false).ConfigureAwait(true)).Succeeded.Should().BeTrue();

        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeFalse("a reparent keeps the geometry reference in the observed graph; no stale demand may be left behind and none may be retired spuriously");

        _ = (await fixture.Authoring.Commands.DeleteNodesAsync(context, [parent.Value!.Id]).ConfigureAwait(true)).Succeeded.Should().BeTrue();

        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue("deleting the new parent must retire the demand through the observers that followed the reparent");
        await WaitForRenderAsync().ConfigureAwait(true);
    });

    /// <summary>A command-driven rename changes no reference, keeps the demand current and leaves the observers live.</summary>
    /// <returns>The asynchronous command-driven rename regression.</returns>
    [TestMethod]
    public Task CommandDrivenRenameKeepsDemandCurrentAndObserversAlive() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        fixture.Activate();
        _ = fixture.Requests.Should().ContainSingle();
        ConfigureStructuralSync(fixture);
        fixture.Authoring.Context.Metadata.IsDirty = true;
        var context = fixture.Authoring.Context;

        _ = (await fixture.Authoring.Commands.RenameNodeAsync(context, fixture.Authoring.Node.Id, "Renamed").ConfigureAwait(true)).Succeeded.Should().BeTrue();

        _ = fixture.Authoring.Node.Name.Should().Be("Renamed");
        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeFalse("a rename changes no reference, so the live demand stays current and none turns stale");

        _ = (await fixture.Authoring.Commands.DeleteNodesAsync(context, [fixture.Authoring.Node.Id]).ConfigureAwait(true)).Succeeded.Should().BeTrue();

        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue("the reference observers must still track the renamed node, so the command-driven delete retires its demand");
        await WaitForRenderAsync().ConfigureAwait(true);
    });

    private static void ConfigureStructuralSync(DemandFixture fixture)
    {
        var accepted = new SyncOutcome(SyncStatus.Accepted, "Structural command", AffectedScope.Empty);
        _ = fixture.Authoring.Sync.Setup(value => value.RenameNodeAsync(It.IsAny<Scene>(), It.IsAny<Guid>(), It.IsAny<string>())).ReturnsAsync(accepted);
        _ = fixture.Authoring.Sync.Setup(value => value.UpdateNodeTransformAsync(It.IsAny<Scene>(), It.IsAny<SceneNode>(), It.IsAny<CancellationToken>())).ReturnsAsync(accepted);
        _ = fixture.Authoring.Sync.Setup(value => value.DetachGeometryAsync(It.IsAny<Scene>(), It.IsAny<Guid>(), It.IsAny<CancellationToken>())).ReturnsAsync(accepted);
        _ = fixture.Authoring.Sync.Setup(value => value.CreateNodeAsync(It.IsAny<SceneNode>(), It.IsAny<Guid?>())).Returns(Task.CompletedTask);
        _ = fixture.Authoring.Sync.Setup(value => value.RemoveNodeHierarchiesAsync(It.IsAny<Scene>(), It.IsAny<IReadOnlyList<Guid>>())).Returns(Task.CompletedTask);
        _ = fixture.Authoring.Sync.Setup(value => value.ReparentNodeAsync(It.IsAny<Scene>(), It.IsAny<Guid>(), It.IsAny<Guid?>(), It.IsAny<bool>())).Returns(Task.CompletedTask);
        _ = fixture.Authoring.Sync.Setup(value => value.ReparentHierarchiesAsync(It.IsAny<Scene>(), It.IsAny<IReadOnlyList<Guid>>(), It.IsAny<Guid?>(), It.IsAny<bool>())).Returns(Task.CompletedTask);
    }
}
