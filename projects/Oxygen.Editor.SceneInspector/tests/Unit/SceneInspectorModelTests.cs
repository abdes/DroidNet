// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Input.Testing;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneInspector.Unit.Tests;

/// <summary>
/// Properties on a model (model editor 5.4): the pane is read-only with the model's reason, and the model's root shows
/// its facts: the source file, the last import and the counts of what it holds.
/// </summary>
[TestClass]
[TestCategory("Inspector")]
internal sealed class SceneInspectorModelTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Car.glb");

    private static readonly ModelFacts CarFacts = new()
    {
        Source = Car,
        LastImport = "Imported",
        SourceFacts = new ModelSourceFacts
        {
            Format = "glb",
            Right = "+X",
            Up = "+Y",
            Front = "+Z",
            NodeCount = 12,
            MeshCount = 5,
            Materials = ["Paint", "Glass"],
            Textures = ["Paint_BaseColor"],
        },
        LodCount = 3,
    };

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task ModelRoot_IsOneCardNamingTheModel()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        using var surfaces = new FakeSurfaceProvider();
        using var sut = CreateSut(documents, scenes, car, scenes.Of(car)!.Scene.RootNodes[0], surfaces, modelFacts: null);

        _ = sut.ShowsModelCard.Should().BeTrue();
        _ = sut.ModelTitle.Should().Be(Car.Name);
        _ = sut.HasEditingNotice.Should().BeFalse("the card offers no edit to refuse");
        _ = sut.CanEditSelectedNodes.Should().BeFalse("the model is read-only");
        _ = sut.HasSceneSettings.Should().BeFalse("the preview's settings are its scene row's, not the model's");
        _ = sut.HasComponentSections.Should().BeFalse("the root's components are the preview's, not the model's");
        _ = sut.PropertyEditors.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ModelRoot_ShowsTheModelsFacts()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var facts = new Mock<IModelFacts>();
        _ = facts.Setup(value => value.ReadAsync(Car, It.IsAny<CancellationToken>())).ReturnsAsync(CarFacts);
        using var surfaces = new FakeSurfaceProvider();
        using var sut = CreateSut(documents, scenes, car, scenes.Of(car)!.Scene.RootNodes[0], surfaces, facts.Object);

        _ = sut.HasModelFacts.Should().BeTrue();
        _ = sut.ModelFactRows.Should().Equal(SceneInspectorViewModel.RowsOf(CarFacts));
    }

    [TestMethod]
    public async Task PreviewSceneSelected_ShowsHowTheModelIsLookedAt()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var facts = new Mock<IModelFacts>();
        using var surfaces = new FakeSurfaceProvider();
        using var sut = CreateSut(documents, scenes, car, selected: null, surfaces, facts.Object);

        _ = sut.HasSceneSettings.Should().BeTrue("the preview scene's own settings are its sun, sky, exposure and post-processing");
        _ = sut.SceneSettingsEditor.HasSceneOwnSettings.Should().BeFalse("a model has no Play settings or references");
        _ = sut.PropertyEditors.Should().Equal([sut.SceneSettingsEditor]);
        _ = sut.ShowsModelCard.Should().BeFalse("the model's card is its root's");
        facts.Verify(value => value.ReadAsync(It.IsAny<AssetPath>(), It.IsAny<CancellationToken>()), Times.Never());
    }

    [TestMethod]
    public void RowsOf_NamesTheSourceTheLastImportAndTheCounts()
        => _ = SceneInspectorViewModel.RowsOf(CarFacts).Should().Equal(
            new ModelFactRow("Source", Car.ToString()),
            new ModelFactRow("Last import", "Imported"),
            new ModelFactRow("Nodes", "12"),
            new ModelFactRow("Meshes", "5"),
            new ModelFactRow("LODs", "3"),
            new ModelFactRow("Materials", "2"),
            new ModelFactRow("Textures", "1"));

    [TestMethod]
    public void RowsOf_UnreadableSource_LeavesItsCountsUnknown()
    {
        var rows = SceneInspectorViewModel.RowsOf(CarFacts with { SourceFacts = null, LodCount = null });

        _ = rows.Where(static row => row.Label is not ("Source" or "Last import")).Should().OnlyContain(static row => row.Value == "Unknown");
    }

    private static SceneInspectorViewModel CreateSut(
        TestSceneDocuments documents,
        DocumentScenes scenes,
        Document document,
        SceneNode? selected,
        FakeSurfaceProvider surfaces,
        IModelFacts? modelFacts)
    {
        var selection = new SceneSelectionService();
        if (selected is not null)
        {
            selection.SetSelection(document.Id, [NodeReference.To(selected.Id)], "Test");
        }

        return new SceneInspectorViewModel(
            new HostingContext
            {
                Dispatcher = null!,
                Application = null!,
                DispatcherScheduler = null!,
            },
            new ViewModelToView(new Mock<IViewLocator>().Object),
            Mock.Of<IAssetReveal>(),
            new TestDocumentArea(document),
            scenes,
            Mock.Of<IAssetFeed>(feed => feed.Items == Observable.Never<IReadOnlyList<AssetEntry>>()),
            Mock.Of<IScenePreview>(),
            selection,
            surfaces,
            Mock.Of<IProjectSession>(session => session.Context == documents.Project.Context),
            new FakeInputSources(),
            modelFacts: modelFacts);
    }
}
