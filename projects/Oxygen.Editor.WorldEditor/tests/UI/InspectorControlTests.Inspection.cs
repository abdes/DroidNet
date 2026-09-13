// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Windowing;
using DroidNet.Documents;
using DroidNet.Mvvm;
using DryIoc;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Documents;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspection;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises the explicit read-only report through WinUI and the document host.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Assets, root files, search, source links and validation remain usable at wide and narrow document widths.</summary>
    /// <param name="width">The report width in DIPs.</param>
    /// <param name="light">Whether to use the light theme.</param>
    /// <returns>The rendered inspection journey.</returns>
    [TestMethod]
    [DataRow(960d, false)]
    [DataRow(560d, true)]
    public Task InspectionDocumentShowsScopedAssetsFilesAndSourceLinks(double width, bool light) => EnqueueAsync(async () =>
    {
        var projects = CreateQueryProject();
        var project = projects.ActiveProject!;
        var metadata = new CookedInspectionDocumentMetadata(project, new("asset:///Content"), validate: false);
        var pipeline = new Mock<IContentPipelineService>(MockBehavior.Strict);
        _ = pipeline.Setup(value => value.InspectCookedOutputAsync(metadata.ScopeUri, It.IsAny<CancellationToken>(), It.IsAny<bool>(), project))
            .Returns((Uri? _, CancellationToken _, bool validate, ProjectContext? _) => Task.FromResult(CreateInspectionReport(project, validate)));
        Uri? navigated = null;
        using var model = new CookedInspectionViewModel(metadata, pipeline.Object, Oxygen.Testing.AssetStatusFixture.EmptyProvider, projects, uri =>
        {
            navigated = uri;
            return Task.FromResult(true);
        });
        var view = new CookedInspectionView { ViewModel = model };
        var root = new Grid { Width = width, Height = 620, RequestedTheme = light ? ElementTheme.Light : ElementTheme.Dark };
        root.Children.Add(view);
        await LoadTestContentAsync(root).ConfigureAwait(true);
        await model.CurrentWork.ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.Assets.Should().HaveCount(2);
        var toolbar = view.FindDescendant<DroidNet.Controls.ToolBar>()!;
        _ = toolbar.OverflowButtonVisibility.Should().Be(Visibility.Collapsed);
        _ = metadata.IsDirty.Should().BeFalse();
        model.SearchText = "Red";
        _ = model.Assets.Should().ContainSingle().Which.Name.Should().Be("Red");
        _ = model.SelectedAsset!.Origin.Should().NotBeNull();
        await model.ShowAssetCommand.ExecuteAsync(model.SelectedAsset.Origin!.SourceAssetUri).ConfigureAwait(true);
        _ = navigated.Should().Be(model.SelectedAsset.Origin.SourceAssetUri);
        var sections = view.FindDescendant<SelectorBar>()!;
        sections.Items[1].IsSelected = true;
        _ = model.Section.Should().Be(1);
        _ = model.Files.Should().ContainSingle();
        sections.Items[0].IsSelected = true;
        await model.ValidateCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        _ = model.StatusText.Should().Be("Output validated");
        _ = model.SelectedAsset!.Name.Should().Be("Red");
        _ = metadata.IsDirty.Should().BeFalse();
        await this.CaptureQueryLayoutAsync(root, string.Create(System.Globalization.CultureInfo.InvariantCulture, $"inspection-{width}-{light}.png")).ConfigureAwait(true);
        pipeline.Verify(value => value.InspectCookedOutputAsync(metadata.ScopeUri, It.IsAny<CancellationToken>(), It.IsAny<bool>(), project), Times.Exactly(2));
        pipeline.VerifyNoOtherCalls();
    });

    /// <summary>Superseded and closed report requests drain without displaying late results or starting overlapping reads.</summary>
    /// <returns>The asynchronous document-lifetime regression.</returns>
    [TestMethod]
    public Task InspectionDocumentDrainsSupersededAndClosedRequests() => EnqueueAsync(async () =>
    {
        var projects = CreateQueryProject();
        var project = projects.ActiveProject!;
        var metadata = new CookedInspectionDocumentMetadata(project, scopeUri: null, validate: false);
        var first = new TaskCompletionSource<CookedOutputReport>(TaskCreationOptions.RunContinuationsAsynchronously);
        var second = new TaskCompletionSource<CookedOutputReport>(TaskCreationOptions.RunContinuationsAsynchronously);
        var calls = 0;
        var pipeline = new Mock<IContentPipelineService>();
        _ = pipeline.Setup(value => value.InspectCookedOutputAsync(It.Is<Uri?>(scope => scope == null), It.IsAny<CancellationToken>(), It.IsAny<bool>(), project))
            .Returns(() => ++calls == 1 ? first.Task : second.Task);
        using var model = new CookedInspectionViewModel(metadata, pipeline.Object, Oxygen.Testing.AssetStatusFixture.EmptyProvider, projects, _ => Task.FromResult(false));
        var original = model.InitializeAsync();
        metadata.RequestRefresh(validate: true);
        _ = calls.Should().Be(1);
        first.SetResult(CreateInspectionReport(project, validate: false));
        await original.ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = calls.Should().Be(2);
        _ = model.Report.Should().BeNull();
        var close = model.CloseAsync(discard: false);
        _ = close.IsCompleted.Should().BeFalse();
        second.SetResult(CreateInspectionReport(project, validate: true));
        await close.ConfigureAwait(true);
        _ = model.Report.Should().BeNull();
        _ = model.IsBusy.Should().BeFalse();
        metadata.RequestRefresh(validate: false);
        _ = calls.Should().Be(2);
    });

    /// <summary>The real document host creates the report editor, disables Save, and reuses the same scope's tab.</summary>
    /// <returns>The asynchronous document-routing regression.</returns>
    [TestMethod]
    public Task InspectionRequestOpensReadOnlyDocumentAndReusesItsTab() => EnqueueAsync(async () =>
    {
        var projects = CreateQueryProject();
        var project = projects.ActiveProject!;
        var messenger = new StrongReferenceMessenger();
        var pipeline = new Mock<IContentPipelineService>();
        _ = pipeline.Setup(value => value.InspectCookedOutputAsync(It.IsAny<Uri?>(), It.IsAny<CancellationToken>(), It.IsAny<bool>(), project))
            .Returns((Uri? _, CancellationToken _, bool validate, ProjectContext? _) => Task.FromResult(CreateInspectionReport(project, validate)));
        var container = new Container();
        await using var containerLifetime = container.ConfigureAwait(true);
        container.RegisterInstance(pipeline.Object);
        container.RegisterInstance<IProjectContextService>(projects);
        container.RegisterInstance(Oxygen.Testing.AssetStatusFixture.EmptyProvider);
        container.RegisterInstance<IMessenger>(messenger);
        var windowId = new WindowId(501);
        var open = new List<IDocumentMetadata>();
        var documents = new Mock<IEditorDocumentService>();
        _ = documents.Setup(value => value.GetOpenDocuments(windowId)).Returns(open);
        _ = documents.Setup(value => value.OpenDocumentAsync(windowId, It.IsAny<IDocumentMetadata>(), -1, It.Is<bool>(select => select)))
            .Returns((WindowId _, IDocumentMetadata metadata, int index, bool select) =>
            {
                open.Add(metadata);
                documents.Raise(value => value.DocumentOpened += null, new DocumentOpenedEventArgs(windowId, metadata, index, select));
                return Task.FromResult(metadata.DocumentId);
            });
        _ = documents.Setup(value => value.SelectDocumentAsync(windowId, It.IsAny<Guid>())).ReturnsAsync(value: true);
        var views = new Mock<IViewLocator>();
        _ = views.Setup(value => value.ResolveView(It.IsAny<object>())).Returns(() => new CookedInspectionView());
        var results = Mock.Of<IOperationResultPublisher>();
        var engine = new Mock<IEngineService>();
        _ = engine.Setup(value => value.ReleaseDocumentSurfacesAsync(It.IsAny<Guid>())).Returns(ValueTask.CompletedTask);
        using var host = new DocumentHostViewModel(
            documents.Object,
            views.Object,
            engine.Object,
            results,
            Mock.Of<IStatusReducer>(),
            container,
            new DocumentCloseCoordinator(Mock.Of<IDocumentClosePrompt>(), results),
            Mock.Of<IWindowManagerService>(),
            windowId);
        using var manager = new DocumentManager(documents.Object, messenger, projects, Mock.Of<IProjectUsageService>(), Mock.Of<IMaterialDocumentService>(), windowId);
        await LoadTestContentAsync(new DocumentHostView { ViewModel = host }).ConfigureAwait(true);
        var scope = new Uri("asset:///Content/Materials");
        _ = (await messenger.Send(new OpenCookedInspectionRequestMessage(project, scope, validate: false)).Response.ConfigureAwait(true)).Should().BeTrue();
        await WaitForRenderAsync().ConfigureAwait(true);
        var model = host.ActiveEditor.Should().BeOfType<CookedInspectionViewModel>().Subject;
        await model.CurrentWork.ConfigureAwait(true);
        _ = host.ActiveEditorView.Should().BeOfType<CookedInspectionView>();
        _ = host.SaveActiveDocumentCommand.CanExecute(parameter: null).Should().BeFalse();
        _ = (await messenger.Send(new OpenCookedInspectionRequestMessage(project, scope, validate: true)).Response.ConfigureAwait(true)).Should().BeTrue();
        await model.CurrentWork.ConfigureAwait(true);
        _ = open.Should().ContainSingle().Which.IsDirty.Should().BeFalse();
        _ = host.ActiveEditor.Should().BeSameAs(model);
        _ = model.StatusText.Should().Be("Output validated");
    });

    private static CookedOutputReport CreateInspectionReport(ProjectContext project, bool validate)
    {
        var root = Path.Combine(project.ProjectRoot, ".cooked", "Content");
        var red = new Uri("asset:///Content/Materials/Red.omat.json");
        var main = new Uri("asset:///Content/Scenes/Main.oscene.json");
        var assets = new[] { new CookedAssetEntry("/Content/Scenes/Main.oscene", ContentCookAssetKind.Scene), new CookedAssetEntry("/Content/Materials/Red.omat", ContentCookAssetKind.Material) };
        CookedFileEntry[] files = [new("container.index.bin", 1024), new("Scenes/Main.oscene", 256), new("Materials/Red.omat", 128)];
        var inspection = new CookInspectionResult(root, Succeeded: true, SourceIdentity: Guid.NewGuid(), assets, files, Diagnostics: []);
        CookedAssetProvenance[] origins =
        [
            new(new("asset:///Content/Scenes/Main.oscene"), main, [red]),
            new(new("asset:///Content/Materials/Red.omat"), red, []),
        ];
        var rootReport = new CookedRootReport("Content", IsPresent: true, inspection, validate ? new(root, Succeeded: true, []) : null, origins);
        return new(project.ProjectId, ScopeUri: null, DateTimeOffset.UtcNow, [rootReport]);
    }
}
