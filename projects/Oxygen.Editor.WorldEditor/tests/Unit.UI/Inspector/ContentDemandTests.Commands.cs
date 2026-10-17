// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Edits.Components;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

/// <summary>The demand service follows the scene document's edits: removing a reference retires its demand.</summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "partial class with public part")]
public sealed partial class ContentDemandTests
{
    /// <summary>Deleting the referencing node retires the activation demand.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task DeletingTheNode_RetiresItsDemand() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        fixture.Activate();
        _ = fixture.Requests.Should().ContainSingle();

        await ApplyAsync(fixture, new RemoveNodes([fixture.Authoring.Node.Id])).ConfigureAwait(true);

        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue();
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Requests.Should().ContainSingle();
    });

    /// <summary>Removing the geometry component retires its demand.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task RemovingTheGeometry_RetiresItsDemand() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        fixture.Activate();
        _ = fixture.Requests.Should().ContainSingle();

        await ApplyAsync(fixture, new RemoveComponent(fixture.Authoring.Node.Id, fixture.Geometry.Id)).ConfigureAwait(true);

        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue();
        await WaitForRenderAsync().ConfigureAwait(true);
    });

    /// <summary>A node and geometry added after activation are observed, so removing the geometry retires its demand.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task AddedNodeAndGeometry_AreObservedAndTheirRemovalRetiresTheDemand() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Activate();
        var scene = fixture.Authoring.Scene;
        var data = SceneNodeTemplates.Empty(scene, "Created");
        await ApplyAsync(fixture, new InsertNodes([SceneNodeTemplates.Append(scene, data)])).ConfigureAwait(true);
        await ApplyAsync(fixture, SceneComponents.Add(fixture.Authoring.Authoring, data.Id, SceneComponents.Geometry)).ConfigureAwait(true);
        await ApplyAsync(fixture, new SetGeometry([data.Id], fixture.GeometryUri)).ConfigureAwait(true);
        var created = scene.AllNodes.Single(node => node.Id == data.Id);

        var selection = new object();
        fixture.Authoring.Messenger.Register<SceneNodeSelectionRequestMessage>(selection, (_, message) => message.Reply([created]));
        fixture.Service.RequestAssignment(scene, [created.Id], fixture.GeometryUri, AssetKind.Geometry);
        _ = fixture.Requests.Should().ContainSingle().Which.uri.Should().Be(fixture.GeometryUri);

        await ApplyAsync(fixture, new RemoveComponent(created.Id, created.Components.OfType<GeometryComponent>().Single().Id)).ConfigureAwait(true);

        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue("the service must observe the added node's components for this removal to retire the demand");
        await WaitForRenderAsync().ConfigureAwait(true);
    });

    /// <summary>A reparent keeps the demand current, and deleting the new parent retires it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task Reparent_KeepsTheDemandUntilTheReferenceIsDeleted() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        fixture.Activate();
        _ = fixture.Requests.Should().ContainSingle();
        var scene = fixture.Authoring.Scene;
        var parent = SceneNodeTemplates.Empty(scene, "Parent");
        await ApplyAsync(fixture, new InsertNodes([SceneNodeTemplates.Append(scene, parent)])).ConfigureAwait(true);

        await ApplyAsync(fixture, new ReparentNodes([fixture.Authoring.Node.Id], parent.Id, preserveWorld: false)).ConfigureAwait(true);

        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeFalse("a reparent keeps the geometry reference in the observed graph");

        await ApplyAsync(fixture, new RemoveNodes([parent.Id])).ConfigureAwait(true);

        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue("deleting the new parent must retire the demand through the observers that followed the reparent");
        await WaitForRenderAsync().ConfigureAwait(true);
    });

    /// <summary>A rename changes no reference: the demand stays current and the observers stay live.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task Rename_KeepsTheDemandAndItsObservers() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        fixture.Activate();
        _ = fixture.Requests.Should().ContainSingle();

        await ApplyAsync(fixture, new RenameNode(fixture.Authoring.Node.Id, "Renamed")).ConfigureAwait(true);

        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeFalse("a rename changes no reference");

        await ApplyAsync(fixture, new RemoveNodes([fixture.Authoring.Node.Id])).ConfigureAwait(true);

        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue("the observers must still track the renamed node");
        await WaitForRenderAsync().ConfigureAwait(true);
    });

    private static async Task ApplyAsync(DemandFixture fixture, ISceneEdit edit)
        => _ = (await fixture.Authoring.Authoring.ExecuteAsync(edit).ConfigureAwait(true)).Succeeded.Should().BeTrue();
}
