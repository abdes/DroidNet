// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core.Diagnostics;
using Oxygen.Managed.Core;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
public sealed partial class ContentDemandTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Opening a scene requests its saved dependencies; metadata and duplicate load messages create no additional cooks.</summary>
    /// <returns>The asynchronous scene-demand regression.</returns>
    [TestMethod]
    public Task SceneActivationDemandsOnlyReferencedAssets() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        fixture.Geometry.OverrideSlots.Add(new MaterialsSlot { Target = fixture.Authoring.TargetFor(fixture.Geometry.Geometry!.Uri), Material = new(fixture.MaterialUri) });
        fixture.Activate();
        _ = fixture.Requests.Select(static request => request.uri).Should().BeEquivalentTo([fixture.GeometryUri, fixture.MaterialUri]);
        fixture.Activate();
        fixture.NotifyEdited();
        _ = fixture.Requests.Should().HaveCount(2);
        fixture.SwitchDocument(Guid.NewGuid());
        _ = fixture.Requests.Should().OnlyContain(request => request.token.IsCancellationRequested);
        await WaitForRenderAsync().ConfigureAwait(true);
    });

    /// <summary>The rendered geometry picker accepts uncooked content immediately, and Undo retires demand without creating a Redo cook.</summary>
    /// <returns>The asynchronous picker and history regression.</returns>
    [TestMethod]
    public Task UncookedGeometryPickerAssignmentOwnsDemandThroughUndo() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Activate();
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", realizeViews: true, contentDemand: fixture.Service, assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        var view = new GeometryView
        {
            ViewModel = model,
            Width = 440
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var choice = model.Groups.Single(group => string.Equals(group.Key, "Content", StringComparison.Ordinal)).Items.Single().Item;
        _ = choice.IsEnabled.Should().BeTrue("a valid saved uncooked geometry must be assignable");
        _ = choice.DisplayType.Should().Contain("Needs cooking");
        var button = (SplitButton)view.FindName("AssetSplitButton");
        await PickAssetAsync(button, "Custom", material: false, this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = fixture.Geometry.Geometry!.Uri.Should().Be(fixture.GeometryUri);
        _ = fixture.Authoring.Context.Metadata.IsDirty.Should().BeTrue("the accepted assignment must dirty the consuming scene");
        _ = fixture.Authoring.Context.History.UndoStack.Should().ContainSingle();
        _ = fixture.Requests.Should().ContainSingle().Which.uri.Should().Be(fixture.GeometryUri);
        await fixture.Authoring.Context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Geometry.Geometry!.Uri.Should().Be(AssetUris.BuildGeneratedUri("BasicShapes/Cube"));
        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue("Undo must detach the original geometry demand");
        await fixture.Authoring.Context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = fixture.Geometry.Geometry!.Uri.Should().Be(fixture.GeometryUri);
        _ = model.SelectedAssetName.Should().Be("Custom");
        _ = fixture.Requests.Should().ContainSingle();
    });

    /// <summary>Material assignment requests its source, and changing selection detaches that demand.</summary>
    /// <returns>The asynchronous material selection regression.</returns>
    [TestMethod]
    public Task MaterialAssignmentDemandEndsWithSelection() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Activate();
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", contentDemand: fixture.Service, assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        await model.RefreshMaterialSlotsAsync().ConfigureAwait(true);
        await model.RefreshMaterialPickerAsync().ConfigureAwait(true);
        await model.ApplyMaterialAsync(new("Material", fixture.MaterialUri, "Material", "/Content/Material.omat.json", AssetPickerGroup.Content, IsEnabled: true, ThumbnailModel: "\uE790")).ConfigureAwait(true);
        _ = fixture.Geometry.OverrideSlots.OfType<MaterialsSlot>().Single().Material.Uri.Should().Be(fixture.MaterialUri);
        _ = fixture.Requests.Should().ContainSingle().Which.uri.Should().Be(fixture.MaterialUri);
        _ = fixture.Authoring.Messenger.Send(new SceneNodeSelectionChangedMessage([]));
        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue();
        await WaitForRenderAsync().ConfigureAwait(true);
    });

    /// <summary>Undo of a material assignment detaches its cook while Redo changes only authoring intent.</summary>
    /// <returns>The asynchronous material-history regression.</returns>
    [TestMethod]
    public Task MaterialDemandRetiresOnUndoWithoutCookingRedo() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Activate();
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", contentDemand: fixture.Service, assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        await model.RefreshMaterialSlotsAsync().ConfigureAwait(true);
        await model.RefreshMaterialPickerAsync().ConfigureAwait(true);
        await model.ApplyMaterialAsync(new("Material", fixture.MaterialUri, "Material", "/Content/Material.omat.json", AssetPickerGroup.Content, IsEnabled: true, ThumbnailModel: "\uE790")).ConfigureAwait(true);
        _ = fixture.Requests.Should().ContainSingle();
        await fixture.Authoring.Context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue();
        await fixture.Authoring.Context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = fixture.Requests.Should().ContainSingle();
        _ = fixture.Geometry.OverrideSlots.OfType<MaterialsSlot>().Single().Material.Uri.Should().Be(fixture.MaterialUri);
    });

    /// <summary>Removing the last scene use retires activation demand without relying on a dirty transition.</summary>
    /// <returns>The asynchronous structural-reference regression.</returns>
    [TestMethod]
    public Task SceneDemandRetiresWhenItsReferencedNodeIsRemoved() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        fixture.Activate();
        _ = fixture.Requests.Should().ContainSingle();
        _ = fixture.Authoring.Scene.RootNodes.Remove(fixture.Authoring.Node);
        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue();
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Requests.Should().ContainSingle();
    });

    /// <summary>A late identity lookup cannot submit work after its scene closes.</summary>
    /// <returns>The asynchronous lookup-lifetime regression.</returns>
    [TestMethod]
    public Task ClosedSceneRejectsLateDemandLookup() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        var resolved = new TaskCompletionSource<ContentBrowserAssetItem?>(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.Assets.Setup(value => value.ResolveAsync(fixture.GeometryUri, It.IsAny<CancellationToken>())).Returns(resolved.Task);
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        fixture.Activate();
        fixture.Authoring.Documents.Raise(value => value.DocumentClosed += null, new DocumentClosedEventArgs(default, fixture.Authoring.Context.Metadata));
        resolved.SetResult(fixture.GeometryAsset);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Requests.Should().BeEmpty();
    });

    /// <summary>Cooked/current identities and engine-owned assets do not launch demand cooks.</summary>
    /// <returns>The asynchronous no-work regression.</returns>
    [TestMethod]
    public Task CurrentAndBuiltinSceneReferencesNeedNoDemandCook() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        _ = fixture.Assets.Setup(value => value.ResolveAsync(fixture.GeometryUri, It.IsAny<CancellationToken>())).ReturnsAsync(fixture.GeometryAsset with { CookStatus = fixture.GeometryAsset.CookStatus! with { Freshness = AssetCookFreshness.Current, OutputAvailability = CookedOutputAvailability.Present } });
        fixture.Geometry.OverrideSlots.Add(new MaterialsSlot { Target = fixture.Authoring.TargetFor(fixture.Geometry.Geometry!.Uri), Material = new(AssetUris.BuildGeneratedUri("Materials/Default")) });
        fixture.Activate();
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Requests.Should().BeEmpty();
        fixture.Assets.Verify(value => value.ResolveAsync(AssetUris.BuildGeneratedUri("Materials/Default"), It.IsAny<CancellationToken>()), Times.Never);
    });

    /// <summary>A delayed accepted edit cannot create demand or overwrite the display for a newer selection.</summary>
    /// <returns>The asynchronous assignment-completion regression.</returns>
    [TestMethod]
    public Task ObsoleteGeometryAssignmentCompletionDoesNotDemandOrReplaceSelection() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Activate();
        var completed = new TaskCompletionSource<SceneCommandResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var commands = new Mock<ISceneDocumentCommandService>();
        _ = commands.Setup(value => value.EditPropertiesAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<Guid>>(), It.IsAny<PropertyEdit>(), It.IsAny<string>(), It.IsAny<EditSessionToken>())).Returns(completed.Task);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", commandService: commands.Object, contentDemand: fixture.Service, assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        var choice = model.Groups.Single(group => string.Equals(group.Key, "Content", StringComparison.Ordinal)).Items.Single().Item;
        var edit = model.ApplyAssetAsync(choice);
        var other = new SceneNode(fixture.Authoring.Scene)
        {
            Name = "Other"
        };
        _ = other.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = new(AssetUris.BuildGeneratedUri("BasicShapes/Sphere")) });
        fixture.Authoring.Scene.RootNodes.Add(other);
        _ = fixture.Authoring.Messenger.Send(new SceneNodeSelectionChangedMessage([other]));
        await WaitForRenderAsync().ConfigureAwait(true);
        var expected = model.SelectedAssetUriString;
        completed.SetResult(SceneCommandResult.Success);
        await edit.ConfigureAwait(true);
        _ = model.SelectedAssetUriString.Should().Be(expected).And.Contain("Sphere");
        _ = fixture.Requests.Should().BeEmpty();
    });

    /// <summary>Hiding the component editor does not discard preview demand for an accepted edit on the same nodes.</summary>
    /// <returns>The asynchronous component-filter regression.</returns>
    [TestMethod]
    public Task AcceptedGeometryAssignmentStillDemandsWhenItsSectionIsHidden() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Activate();
        var completed = new TaskCompletionSource<SceneCommandResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var commands = new Mock<ISceneDocumentCommandService>();
        _ = commands.Setup(value => value.EditPropertiesAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<Guid>>(), It.IsAny<PropertyEdit>(), It.IsAny<string>(), It.IsAny<EditSessionToken>())).Returns(completed.Task);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", commandService: commands.Object, contentDemand: fixture.Service, assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        var choice = model.Groups.Single(group => string.Equals(group.Key, "Content", StringComparison.Ordinal)).Items.Single().Item;
        var edit = model.ApplyAssetAsync(choice);
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        model.SetInputEnabled(enabled: false);
        completed.SetResult(SceneCommandResult.Success);
        await edit.ConfigureAwait(true);
        _ = fixture.Requests.Should().ContainSingle().Which.uri.Should().Be(fixture.GeometryUri);
    });
}
