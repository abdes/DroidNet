// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Models;
using Oxygen.Editor.Models.Documents;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Scenes.Tests.Documents;

/// <summary>
/// The scene each open document shows (model editor 4): a scene document's own scene, and a model document's preview
/// scene, built in memory with one instance root of the model at the origin and a directional light, never saved and
/// read-only.
/// </summary>
[TestClass]
public sealed class DocumentScenesTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Car.glb");
    private static readonly AssetPath MovedCar = AssetPath.Parse("/Content/Vehicles/Car.glb");

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task Of_SceneDocument_IsItsOwnContent()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var document = await documents.OpenAsync(SampleScene.Build()).ConfigureAwait(false);

        _ = scenes.Of(document).Should().BeSameAs(SceneContent.Of(document));
        _ = DocumentScenes.ShowsModel(document).Should().BeFalse();
    }

    [TestMethod]
    public async Task Of_ModelDocument_IsAPreviewWithOneInstanceOfTheModelNamedAfterItAndALight()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var document = await documents.OpenModelAsync(Car).ConfigureAwait(false);

        var preview = scenes.Of(document);

        _ = DocumentScenes.ShowsModel(document).Should().BeTrue();
        _ = preview.Should().NotBeNull();
        _ = preview!.Scene.RootNodes.Should().HaveCount(2);
        var root = preview.Scene.RootNodes[0];
        _ = root.Metadata.Name.Should().Be("Car.glb", "the instance root bears the document's title");
        _ = root.Components.OfType<SceneInstanceComponent>().Should().ContainSingle().Which.Source.Should().Be(Car);
        _ = preview.Scene.RootNodes[1].Components.OfType<DirectionalLightComponent>().Should().ContainSingle();
        _ = preview.Authoring.Layout.Select(static entry => entry.NodeId).Should().Equal(
            [root.Id],
            "the explorer lists the model; the light is no part of it");
    }

    [TestMethod]
    public async Task ReferenceOf_AModelsNode_IsThatNodeBelowThePreviewsInstance()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var model = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var scene = await documents.OpenAsync(SampleScene.Build()).ConfigureAwait(false);
        var node = NodeId.New();

        var reference = scenes.ReferenceOf(model, node);

        _ = reference.Should().Be(new NodeReference(scenes.Of(model)!.Scene.RootNodes[0].Id, NodePath.Of(node)));
        _ = scenes.ReferenceOf(scene, node).Should().BeNull("a scene document shows no model");
    }

    [TestMethod]
    public async Task Of_ModelDocument_IsTheSamePreviewUntilItsContentChanges()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var document = await documents.OpenModelAsync(Car).ConfigureAwait(false);

        var first = scenes.Of(document);

        _ = scenes.Of(document).Should().BeSameAs(first);
        _ = scenes.DocumentOf(first!.Scene).Should().BeSameAs(document);
    }

    [TestMethod]
    public async Task PreviewEdit_IsRefusedAsReadOnly()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var document = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var preview = scenes.Of(document)!;
        var probe = new ProbeEdit { Targets = [preview.Scene.RootNodes[0].Id] };

        var result = await preview.Authoring.ExecuteAsync(probe).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.SceneReadOnly);
        _ = probe.Log.Calls.Should().BeEmpty();
        _ = preview.Authoring.ReadOnlyReason.Should().NotBeNull();
    }

    [TestMethod]
    public async Task SceneDocumentEdit_IsNotReadOnly()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var document = await documents.OpenAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await scenes.Of(document)!.Authoring.ExecuteAsync(new ProbeEdit { Targets = [SampleScene.B] }).ConfigureAwait(false);

        SceneAssert.Applied(result);
    }

    [TestMethod]
    public async Task ModelDocumentClosed_ReopeningBuildsANewPreview()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var document = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var closed = scenes.Of(document)!.Scene;

        documents.Open.Close(document);
        var reopened = await documents.OpenModelAsync(Car).ConfigureAwait(false);

        _ = scenes.DocumentOf(closed).Should().BeNull();
        _ = scenes.Of(reopened)!.Scene.Should().NotBeSameAs(closed);
    }

    [TestMethod]
    public async Task ModelMoved_PreviewInstancePlacesTheMovedSource()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var document = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var preview = scenes.Of(document)!;
        var moved = new DocumentFile(documents.Project.PathOf(MovedCar.ToString()), MovedCar);

        await document.FollowRelocationAsync(
            new DocumentRelocation(moved, document.Version, asset => asset == Car ? MovedCar : null),
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = ((ModelContent)document.Content).Source.Should().Be(MovedCar);
        _ = scenes.Of(document).Should().BeSameAs(preview);
        _ = preview.Scene.RootNodes[0].Components.OfType<SceneInstanceComponent>().Single().Source.Should().Be(MovedCar);
    }

    [TestMethod]
    public async Task Of_ModelDocument_LightsThePreviewWithASkyAndItsSunAtAFixedExposure()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var document = await documents.OpenModelAsync(Car).ConfigureAwait(false);

        var preview = scenes.Of(document)!.Scene;

        var sun = preview.RootNodes[1].Components.OfType<DirectionalLightComponent>().Single();
        _ = sun.AtmosphereSlot.Should().Be(AtmosphereLightSlot.Primary, "the sky atmosphere takes the preview's light as its sun");
        _ = sun.AffectsWorld.Should().BeTrue();
        _ = preview.Environment.AtmosphereEnabled.Should().BeTrue();
        _ = preview.Environment.SkyLight.Enabled.Should().BeTrue();
        _ = preview.Environment.PostProcess.ExposureMode.Should().Be(ExposureMode.Manual, "the background never drives the model's exposure");
        _ = scenes.EnvironmentOf(document).Should().Be(PreviewEnvironment.Default);
    }

    [TestMethod]
    public async Task SetPreviewEnvironment_HidesTheSkyAndSunAndCompensatesTheExposureOutsideHistory()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var document = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var preview = scenes.Of(document)!;
        var exposure = preview.Scene.Environment.PostProcess.ManualExposureEv;
        using var changes = new ChangeRecorder(preview.Authoring);
        var hidden = new PreviewEnvironment(IsShown: false, ExposureCompensationEv: 1.5f);

        await scenes.SetPreviewEnvironmentAsync(document, hidden).ConfigureAwait(false);

        var environment = preview.Scene.Environment;
        _ = environment.AtmosphereEnabled.Should().BeFalse();
        _ = environment.SkyLight.Enabled.Should().BeFalse();
        _ = environment.PostProcess.ManualExposureEv.Should().Be(exposure, "the fixed exposure stays; the compensation adds to it");
        _ = environment.PostProcess.ExposureCompensationEv.Should().Be(1.5f);
        _ = preview.Scene.RootNodes[1].Components.OfType<DirectionalLightComponent>().Single().AffectsWorld.Should().BeFalse();
        _ = scenes.EnvironmentOf(document).Should().Be(hidden);
        _ = document.History.UndoStack.Should().BeEmpty("the preview takes no edit");
        _ = changes.Only().OfType<SceneChange.PropertiesChanged>().Select(static change => change.Node)
            .Should().BeEquivalentTo([(NodeId?)null, preview.Scene.RootNodes[1].Id], "the engine shows the environment and the sun");
    }

    [TestMethod]
    public async Task PreviewEnvironmentEdit_IsTakenWithoutChangingTheModelsDocument()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var document = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var preview = scenes.Of(document)!;
        var environment = preview.Scene.Environment with
        {
            SkyAtmosphere = preview.Scene.Environment.SkyAtmosphere with { SunDiskEnabled = false },
        };

        var result = await preview.Authoring.ExecuteAsync(new SetSceneEnvironment(environment, "Sun disk")).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = preview.Scene.Environment.SkyAtmosphere.SunDiskEnabled.Should().BeFalse();
        _ = preview.Authoring.History.UndoStack.Should().ContainSingle("the preview keeps its own history");
        _ = document.History.UndoStack.Should().BeEmpty("the model's document never shows a change of its preview");
        _ = document.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task Preview_TakesEditsOfItsSunOnly()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var document = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var preview = scenes.Of(document)!;

        _ = preview.Authoring.TakesEditsOf(preview.Scene.RootNodes[1].Id).Should().BeTrue("the sun is how the model is looked at");
        _ = preview.Authoring.TakesEditsOf(preview.Scene.RootNodes[0].Id).Should().BeFalse("the instance root is the model");
    }

    [TestMethod]
    public async Task SetPreviewEnvironment_KeepsTheEnvironmentsOtherSettings()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var document = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var preview = scenes.Of(document)!;
        var environment = preview.Scene.Environment with
        {
            SkyAtmosphere = preview.Scene.Environment.SkyAtmosphere with { SunDiskEnabled = false },
        };
        SceneAssert.Applied(await preview.Authoring.ExecuteAsync(new SetSceneEnvironment(environment, "Sun disk")).ConfigureAwait(false));

        await scenes.SetPreviewEnvironmentAsync(document, new PreviewEnvironment(IsShown: false, ExposureCompensationEv: 2f)).ConfigureAwait(false);
        await scenes.SetPreviewEnvironmentAsync(document, PreviewEnvironment.Default).ConfigureAwait(false);

        _ = preview.Scene.Environment.SkyAtmosphere.SunDiskEnabled.Should().BeFalse("the toggle shows and hides the sky, and keeps how it was tuned");
        _ = scenes.EnvironmentOf(document).Should().Be(PreviewEnvironment.Default);
    }

    [TestMethod]
    public async Task PreviewChange_IsKeptBesideTheModelAndShownWhenItReopens()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open, session: documents.Session, files: documents.Files);
        var document = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var preview = scenes.Of(document)!;
        SceneAssert.Applied(await preview.Authoring.ExecuteAsync(new SetSceneEnvironment(WithoutSunDisk(preview.Scene), "Sun disk")).ConfigureAwait(false));

        await scenes.FlushAsync().ConfigureAwait(false);
        documents.Open.Close(document);
        var reopened = scenes.Of(await documents.OpenModelAsync(Car).ConfigureAwait(false))!;

        var file = documents.Project.PathOf(AssetFileTypes.PreviewOf(Car.ToString()));
        _ = documents.Project.Context.FileSystem.File.Exists(file.FullPath).Should().BeTrue("the preview is kept beside its model");
        _ = reopened.Scene.Environment.SkyAtmosphere.SunDiskEnabled.Should().BeFalse("the model reopens as it was left");
        _ = reopened.Scene.RootNodes.Should().HaveCount(2, "the file keeps the sun; the model is placed again from its source");
    }

    [TestMethod]
    public async Task ResetPreview_ForgetsTheFileAndShowsTheDefaultPreview()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open, session: documents.Session, files: documents.Files);
        var document = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var preview = scenes.Of(document)!;
        SceneAssert.Applied(await preview.Authoring.ExecuteAsync(new SetSceneEnvironment(WithoutSunDisk(preview.Scene), "Sun disk")).ConfigureAwait(false));
        await scenes.FlushAsync().ConfigureAwait(false);

        _ = (await scenes.ResetPreviewAsync(document, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        var file = documents.Project.PathOf(AssetFileTypes.PreviewOf(Car.ToString()));
        _ = documents.Project.Context.FileSystem.File.Exists(file.FullPath).Should().BeFalse();
        _ = scenes.Of(document)!.Scene.Environment.SkyAtmosphere.SunDiskEnabled.Should().BeTrue();
    }

    [TestMethod]
    public async Task UnreadablePreviewFile_ShowsTheDefaultPreview()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open, session: documents.Session, files: documents.Files);
        var file = documents.Project.PathOf(AssetFileTypes.PreviewOf(Car.ToString()));
        _ = documents.Project.Context.FileSystem.Directory.CreateDirectory(documents.Project.Context.FileSystem.Path.GetDirectoryName(file.FullPath)!);
        await documents.Project.Context.FileSystem.File.WriteAllTextAsync(file.FullPath, "{ not json", this.TestContext.CancellationToken).ConfigureAwait(false);

        var document = await documents.OpenModelAsync(Car).ConfigureAwait(false);

        _ = scenes.Of(document)!.Scene.Environment.SkyAtmosphere.SunDiskEnabled.Should().BeTrue("an unreadable preview warns and shows the default");
    }

    [TestMethod]
    public async Task PreviewScene_IsIdentifiedByTheModelsSourceIdentity()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open, session: documents.Session, files: documents.Files);
        var document = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var identity = Guid.CreateVersion7();
        var root = documents.Project.Context.ProjectRoot.FullPath;
        var record = ModelImportSettings.PathFor(root, identity);
        var files = documents.Project.Context.FileSystem;
        _ = files.Directory.CreateDirectory(files.Path.GetDirectoryName(record)!);
        using (var settings = JsonDocument.Parse("{}"))
        {
            var created = ModelImportSettings.Create(record, document.File.Path.FullPath, Car.VirtualPath, settings.RootElement);
            await files.File.WriteAllBytesAsync(record, created.ToBytes(), this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        _ = scenes.Of(document)!.Scene.Id.Should().Be(new SceneId(identity), "per-user view state, such as the camera, survives the model's renames");
    }

    [TestMethod]
    public async Task Disposed_ShowsNoPreview()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        var scenes = new DocumentScenes(documents.Open);
        var document = await documents.OpenModelAsync(Car).ConfigureAwait(false);

        scenes.Dispose();

        _ = scenes.Of(document).Should().BeNull();
    }

    private static SceneEnvironmentData WithoutSunDisk(Scene scene)
        => scene.Environment with { SkyAtmosphere = scene.Environment.SkyAtmosphere with { SunDiskEnabled = false } };
}
