// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Testing;
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

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

/// <summary>
/// The preview's sun in Properties (model editor 4, 5.4): it is how the model is looked at, not the model, so its
/// sections take input, without the read-only notice, and it shows no model facts.
/// </summary>
[TestClass]
internal sealed class PreviewSunInspectionTests : VisualUserInterfaceTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Car.glb");

    public TestContext TestContext { get; set; } = null!;

    /// <summary>The selected sun takes edits and shows no facts.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task PreviewSunTakesEditsAndShowsNoFacts() => EnqueueAsync(async () =>
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var scenes = new DocumentScenes(documents.Open);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(true);
        var facts = new Mock<IModelFacts>();
        using var surfaces = new FakeSurfaceProvider();
        var selection = new SceneSelectionService();
        selection.SetSelection(car.Id, [NodeReference.To(scenes.Of(car)!.Scene.RootNodes[1].Id)], "Test");
        var dispatcher = VisualUserInterfaceTestsApp.DispatcherQueue;
        using var sut = new SceneInspectorViewModel(
            new HostingContext { Application = Application.Current, Dispatcher = dispatcher, DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher) },
            new ViewModelToView(new Mock<IViewLocator>().Object),
            Mock.Of<IAssetReveal>(),
            new TestDocumentArea(car),
            scenes,
            Mock.Of<IAssetFeed>(feed => feed.Items == Observable.Never<IReadOnlyList<AssetEntry>>()),
            Mock.Of<IScenePreview>(),
            selection,
            surfaces,
            Mock.Of<IProjectSession>(session => session.Context == documents.Project.Context),
            new FakeInputSources(),
            modelFacts: facts.Object);

        _ = sut.ReadOnlyReason.Should().BeNull("the preview's sun is how the model is looked at, not the model");
        _ = sut.CanEditSelectedNodes.Should().BeTrue();
        _ = sut.HasEditingNotice.Should().BeFalse();
        _ = sut.ShowsModelCard.Should().BeFalse();
        facts.Verify(value => value.ReadAsync(It.IsAny<AssetPath>(), It.IsAny<CancellationToken>()), Times.Never());
    });
}
