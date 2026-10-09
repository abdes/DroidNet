// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage;
using Moq;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

public sealed partial class SceneDocumentCommandServiceTests
{
    private static readonly Uri RedMaterial = new("asset:///Content/Materials/Red.omat.json");
    private static readonly Uri BlueMaterial = new("asset:///Content/Materials/Blue.omat.json");
    private static readonly Uri GreenMaterial = new("asset:///Content/Materials/Green.omat.json");

    /// <summary>
    /// An open scene follows a rename in memory: only referencing nodes change, nothing enters history, the document
    /// stays clean, the rewritten file becomes its saved state, and only those nodes refresh after the cook publishes.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task OpenSceneFollowsRelocationWithoutReloadOrHistory()
    {
        var projectManager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        var written = new FileVersion(Exists: true, new string('B', 64));
        _ = projectManager.Setup(value => value.RecordSceneSourceRewrite(It.IsAny<Scene>(), "C:/scene.oscene.json", written));
        var fixture = CreateFixture(projectManager: projectManager.Object);
        var scene = CreateScene();
        var red = AddSlotGeometry(scene, "Red", "Cube");
        var plain = AddSlotGeometry(scene, "Plain", "Sphere");
        AssignSlot(red, RedMaterial);
        var context = CreateContext(scene);
        _ = fixture.Sync.Setup(value => value.GetDocumentScene(context.Metadata)).Returns(scene);
        _ = fixture.Sync.Setup(value => value.AttachGeometryAsync(scene, red, It.IsAny<CancellationToken>()))
            .ReturnsAsync(new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditGeometry, AffectedScope.Empty));
        var map = new RelocationMap();
        map.AddIdentity("/Content/Materials/Red.omat", "/Content/Materials/Blue.omat", isFolder: false);
        var change = new AssetRelocationChange(map, [], Task.FromResult<string?>(null));

        var result = await fixture.Sut.FollowRelocationAsync(context, change, new RelocatedFile("C:/scene.oscene.json", "C:/scene.oscene.json", written)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = red.Components.OfType<GeometryComponent>().Single().OverrideSlots.OfType<MaterialsSlot>().Single().Material.Uri.Should().Be(BlueMaterial);
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = context.Metadata.IsDirty.Should().BeFalse();
        projectManager.Verify(value => value.RecordSceneSourceRewrite(scene, "C:/scene.oscene.json", written), Times.Once);
        fixture.Sync.Verify(value => value.AttachGeometryAsync(scene, red, It.IsAny<CancellationToken>()), Times.Once);
        fixture.Sync.Verify(value => value.AttachGeometryAsync(scene, plain, It.IsAny<CancellationToken>()), Times.Never);
    }

    /// <summary>Undo restores a material captured before a rename at the material's current identity.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task UndoResolvesMaterialRenamedAfterTheEdit()
    {
        var redirects = new FakeRedirects { Renames = { [RedMaterial] = BlueMaterial } };
        var (fixture, context, geometry) = await AssignTwiceAsync(redirects).ConfigureAwait(false);

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = geometry.OverrideSlots.OfType<MaterialsSlot>().Single().Material.Uri.Should().Be(BlueMaterial);
        _ = fixture.Results.Published.Should().NotContain(static result => result.Title == "Restored a reference to a deleted asset");
    }

    /// <summary>Undo of an edit whose material was deleted restores it as missing, warns, and never blocks history.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task UndoRestoresDeletedMaterialAsMissingWithWarning()
    {
        var redirects = new FakeRedirects { Deleted = { RedMaterial } };
        var (fixture, context, geometry) = await AssignTwiceAsync(redirects).ConfigureAwait(false);

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = geometry.OverrideSlots.OfType<MaterialsSlot>().Single().Material.Uri.Should().Be(RedMaterial);
        _ = fixture.Results.Published.Should().Contain(static result => result.Title == "Restored a reference to a deleted asset" && result.Severity == DiagnosticSeverity.Warning);
        _ = context.History.UndoStack.Should().ContainSingle("the earlier assignment stays undoable");
    }

    private static async Task<(Fixture fixture, SceneDocumentCommandContext context, GeometryComponent geometry)> AssignTwiceAsync(IAssetRedirects redirects)
    {
        var fixture = CreateFixture(redirects: redirects);
        var scene = CreateScene();
        var node = AddSlotGeometry(scene, "Cube", "Cube");
        var geometry = node.Components.OfType<GeometryComponent>().Single();
        var target = TestSlotTarget(geometry.Geometry!.Uri);
        _ = fixture.Sync.Setup(value => value.UpdateMaterialSlotAsync(scene, node, target, It.IsAny<Uri?>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditMaterialSlot, AffectedScope.Empty));
        _ = fixture.Sync.Setup(value => value.RestoreMaterialSlotAsync(scene, node, target, It.IsAny<Uri?>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditMaterialSlot, AffectedScope.Empty));
        var context = CreateContext(scene);
        _ = (await fixture.Sut.EditMaterialSlotAsync(context, [node.Id], target, RedMaterial, EditSessionToken.OneShot).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = (await fixture.Sut.EditMaterialSlotAsync(context, [node.Id], target, GreenMaterial, EditSessionToken.OneShot).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        return (fixture, context, geometry);
    }

    private static void AssignSlot(SceneNode node, Uri material)
    {
        var geometry = node.Components.OfType<GeometryComponent>().Single();
        geometry.OverrideSlots.Add(new MaterialsSlot { Target = TestSlotTarget(geometry.Geometry!.Uri), Material = new AssetReference<MaterialAsset>(material) });
    }

    private sealed class FakeRedirects : IAssetRedirects
    {
        public Dictionary<Uri, Uri> Renames { get; } = [];

        public HashSet<Uri> Deleted { get; } = [];

        public string Resolve(string reference) => this.Resolve(new Uri(reference)).ToString();

        public Uri Resolve(Uri reference) => this.Renames.TryGetValue(reference, out var current) ? current : reference;

        public bool WasDeleted(Uri reference) => this.Deleted.Contains(reference);
    }
}
