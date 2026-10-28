// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using AwesomeAssertions;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Documents;
using Oxygen.Editor.ModelEditor.TestSupport;
using Oxygen.Editor.Models.Documents;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.Viewports;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ModelEditor.Tests;

/// <summary>
/// The Model Editor's toolbar (model editor 5.1, scene instances 8.6): <em>Add to scene</em> places the model in the
/// open scene, <em>Reimport</em> is enabled whenever the source is there and no import of the model runs, and
/// <em>Browse to asset</em> reveals the model file when nothing is selected. Its tab never leaves its window.
/// </summary>
[TestClass]
public sealed class ModelEditorViewModelTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Car.glb");

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task Provider_KeepsTheTabInItsWindow()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var services = new TestModelEditorServices(documents);

        var provider = new ModelEditorProvider(documents.ModelKind, services.Services);

        _ = provider.Editor.CanTearOut.Should().BeFalse();
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        using var tab = new DocumentTab(car, provider.Editor);
        _ = tab.CanTearOut.Should().BeFalse();
    }

    [TestMethod]
    public async Task AddToScene_NoSceneOpen_IsDisabled()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var services = new TestModelEditorServices(documents);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        using var editor = CreateEditor(car, services);

        _ = editor.AddToSceneCommand.CanExecute(parameter: null).Should().BeFalse();
    }

    [TestMethod]
    public async Task AddToScene_ShowsTheSceneThenPlacesTheModelInIt()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var services = new TestModelEditorServices(documents);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var harbor = await documents.OpenAsync(new Scene { Name = "Harbor" }.Dehydrate()).ConfigureAwait(false);
        var sceneView = new Mock<ISceneView>();
        _ = sceneView.Setup(value => value.AddInstanceAsync(Car)).ReturnsAsync(true);
        services.Views.Attach(harbor.Id, sceneView.Object);
        var area = new TestDocumentArea(car, harbor);
        using var editor = CreateEditor(car, services);
        editor.AttachWindow(WindowOf(area));

        _ = editor.AddToSceneCommand.CanExecute(parameter: null).Should().BeTrue();
        await editor.AddToSceneCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = area.ActiveDocument.Should().BeSameAs(harbor, "the instance goes in front of the scene's viewport, where the user sees it");
        sceneView.Verify(value => value.AddInstanceAsync(Car), Times.Once());
    }

    [TestMethod]
    [DataRow(AssetCookFreshness.OutOfDate, true)]
    [DataRow(AssetCookFreshness.InvalidSource, true)]
    [DataRow(AssetCookFreshness.Current, false)]
    public async Task Reimport_IsEnabledWhateverTheImportSays_TheStatusOnlyWhenItNeedsOne(AssetCookFreshness freshness, bool reported)
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var services = new TestModelEditorServices(documents);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        using var editor = CreateEditor(car, services);

        services.Assets.OnNext([ProductOf(Car, freshness)]);

        _ = editor.ReimportCommand.CanExecute(parameter: null).Should().BeTrue("the user may reimport a current import too");
        _ = editor.ImportStatusText.Should().Be(reported ? ModelImportStatus.TextOf(editor.ImportState) : string.Empty, "a current import says nothing");
        _ = editor.HasImportStatus.Should().Be(reported);
        _ = editor.ImportStatusSeverity.Should().Be(reported ? ModelImportStatus.SeverityOf(editor.ImportState) : DiagnosticSeverity.Info);
    }

    [TestMethod]
    public async Task BrowseToAsset_NothingSelected_RevealsTheModelFile()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var services = new TestModelEditorServices(documents);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        var reveal = new Mock<IAssetReveal>();
        using var editor = CreateEditor(car, services);
        editor.AttachWindow(WindowOf(new TestDocumentArea(car), reveal.Object));

        await editor.BrowseToAssetCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        reveal.Verify(value => value.RevealAsync(It.Is<IReadOnlyList<AssetPath>>(assets => assets.SequenceEqual(new[] { Car }))), Times.Once());
    }

    [TestMethod]
    public async Task Editor_TakesNoInstance()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var services = new TestModelEditorServices(documents);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        using var editor = CreateEditor(car, services);

        _ = (await editor.AddInstanceAsync(Car).ConfigureAwait(false)).Should().BeFalse("a model is placed in a scene, never in itself");
    }

    [TestMethod]
    public async Task Viewport_LooksAtTheModelAndLightsItsPreview()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var services = new TestModelEditorServices(documents);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        _ = services.Preview.Setup(value => value.IsReady(car.Id)).Returns(value: true);
        using var editor = CreateEditor(car, services);
        var viewport = await this.ViewportOfAsync(editor).ConfigureAwait(false);

        viewport.ShowPreviewEnvironment = false;
        viewport.ExposureCompensation = -1.0;

        _ = viewport.Profile.Should().Be(ViewportProfile.Viewing, "a model is looked at, not edited (model editor 5.2)");
        _ = services.Scenes.EnvironmentOf(car).Should().Be(new PreviewEnvironment(IsShown: false, ExposureCompensationEv: -1f));
    }

    [TestMethod]
    public async Task Framing_RunsInTheViewportOnceTheModelShows()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var services = new TestModelEditorServices(documents);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        using (var hidden = CreateEditor(car, services))
        {
            _ = hidden.CanExecute(ShellCommands.FrameAll.Id).Should().BeFalse("no viewport shows the model yet");
        }

        _ = services.Preview.Setup(value => value.IsReady(car.Id)).Returns(value: true);
        using var shown = CreateEditor(car, services);
        _ = await this.ViewportOfAsync(shown).ConfigureAwait(false);

        _ = shown.CanExecute(ShellCommands.FrameSelection.Id).Should().BeTrue();
        _ = shown.CanExecute(ShellCommands.FrameAll.Id).Should().BeTrue();
        _ = shown.CanExecute(ShellCommands.Delete.Id).Should().BeFalse("a model takes no edit");
    }

    [TestMethod]
    public async Task ResetPreview_ReturnsThePreviewToTheDefault()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var services = new TestModelEditorServices(documents);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        using var editor = CreateEditor(car, services);
        await services.Scenes.SetPreviewEnvironmentAsync(car, new PreviewEnvironment(IsShown: false, ExposureCompensationEv: 2f)).ConfigureAwait(false);

        var before = services.Scenes.Of(car)!.Scene;

        await editor.ResetPreviewCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        var rebuilt = services.Scenes.Of(car)!.Scene;
        _ = rebuilt.Should().NotBeSameAs(before, "the document reloads, which builds the preview again");
        _ = rebuilt.Environment.AtmosphereEnabled.Should().BeTrue();
        _ = services.Scenes.EnvironmentOf(car).Should().Be(PreviewEnvironment.Default);
    }

    [TestMethod]
    public async Task Viewport_KeepsItsCameraPerUserUnderThePreviewScene()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        using var services = new TestModelEditorServices(documents);
        var car = await documents.OpenModelAsync(Car).ConfigureAwait(false);
        _ = services.Preview.Setup(value => value.IsReady(car.Id)).Returns(value: true);
        var editor = CreateEditor(car, services);
        var viewport = await this.ViewportOfAsync(editor).ConfigureAwait(false);
        viewport.ShowGrid = false;

        editor.Dispose();

        var kept = await services.Services.ViewportStates.RestoreAsync(services.Scenes.Of(car)!.Scene.Id).ConfigureAwait(false);
        _ = kept.Should().NotBeNull("the camera and view options are kept per user, as a scene's are");
        _ = kept!.Panes.Should().ContainSingle().Which.ShowGrid.Should().BeFalse();
    }

    private static ModelEditorViewModel CreateEditor(Document model, TestModelEditorServices services)
    {
        var tab = new DocumentTab(model, new ModelEditorProvider((ModelDocumentKind)model.Kind, services.Services).Editor);
        return new ModelEditorViewModel(tab, services.Services);
    }

    private static DocumentWindowServices WindowOf(TestDocumentArea area, IAssetReveal? reveal = null)
        => new(new WindowId(1), area, Mock.Of<ICommandService>(), Mock.Of<IPlacementService>(), reveal);

    private static AssetEntry ProductOf(AssetPath source, AssetCookFreshness freshness)
    {
        var product = AssetFileTypes.SceneProductOf(source)!;
        return new AssetEntry(product, product.Name, AssetKind.Scene, AssetState.Cooked, DerivedState: null, AssetRuntimeAvailability.NotApplicable, product.VirtualPath, SourcePath: null, DescriptorPath: null, CookedPath: null, AssetGuid: null, [], IsSelectable: true)
        {
            CookStatus = new AssetCookStatus(product, freshness, HasPublishedOutput: true, CookedOutputAvailability.Present, [], []),
        };
    }

    // The viewport is made once its kept state is read.
    private async Task<ViewportViewModel> ViewportOfAsync(ModelEditorViewModel editor)
    {
        var shown = new TaskCompletionSource<ViewportViewModel>(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnChanged(object? sender, PropertyChangedEventArgs args)
        {
            if (editor.Viewport is { } viewport)
            {
                _ = shown.TrySetResult(viewport);
            }
        }

        editor.PropertyChanged += OnChanged;
        try
        {
            OnChanged(editor, new PropertyChangedEventArgs(nameof(ModelEditorViewModel.Viewport)));
            return await shown.Task.WaitAsync(TimeSpan.FromSeconds(10), this.TestContext.CancellationToken).ConfigureAwait(false);
        }
        finally
        {
            editor.PropertyChanged -= OnChanged;
        }
    }
}
