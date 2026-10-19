// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Inspection;
using Oxygen.Managed.Core;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspection;

[TestClass]
internal sealed class InspectionTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Assets, root files, search, source links and validation remain usable at wide and narrow panel widths.</summary>
    /// <param name="width">The report width in DIPs.</param>
    /// <param name="light">Whether to use the light theme.</param>
    /// <returns>The rendered inspection journey.</returns>
    [TestMethod]
    [DataRow(960d, false)]
    [DataRow(560d, true)]
    public Task InspectionShowsScopedAssetsFilesAndSourceLinks(double width, bool light) => EnqueueAsync(async () =>
    {
        var projects = CreateQueryProject();
        var project = projects.ActiveProject!;
        var scope = new CookedInspectionScope(project, AssetPath.Content, Validate: false);
        var pipeline = new Mock<IContentPipelineService>(MockBehavior.Strict);
        _ = pipeline.Setup(value => value.InspectCookedOutputAsync(scope.Scope, It.IsAny<CancellationToken>(), It.IsAny<bool>(), project)).Returns((AssetPath? _, CancellationToken _, bool validate, ProjectContext? _) => Task.FromResult(CreateInspectionReport(project, validate)));
        AssetPath? navigated = null;
        using var model = new CookedInspectionViewModel(pipeline.Object, NoAssets(), projects, asset =>
        {
            navigated = asset;
            return Task.FromResult(true);
        });
        var view = new CookedInspectionView
        {
            ViewModel = model,
        };
        var root = new Grid
        {
            Width = width,
            Height = 620,
            RequestedTheme = light ? ElementTheme.Light : ElementTheme.Dark,
        };
        root.Children.Add(view);
        await LoadTestContentAsync(root).ConfigureAwait(true);
        await model.InspectAsync(scope).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.Assets.Should().HaveCount(2);
        var toolbar = view.FindDescendant<DroidNet.Controls.ToolBar>()!;
        _ = toolbar.OverflowButtonVisibility.Should().Be(Visibility.Collapsed);
        model.SearchText = "Red";
        _ = model.Assets.Should().ContainSingle().Which.Name.Should().Be("Red");
        _ = model.SelectedAsset!.Origin.Should().NotBeNull();
        await model.ShowAssetCommand.ExecuteAsync(model.SelectedAsset.Origin!.SourceAssetUri).ConfigureAwait(true);
        _ = navigated.Should().Be(AssetPath.Parse("/Content/Materials/Red.omat"), "a source is named by its identity, its path without the authoring .json");
        var sections = view.FindDescendant<SelectorBar>()!;
        sections.SelectedItem = sections.Items[1];
        _ = model.Section.Should().Be(1);
        _ = model.Files.Should().ContainSingle();
        _ = sections.Items.Where(static item => item.IsSelected).Should().ContainSingle().Which.Should().BeSameAs(sections.Items[1]);
        model.Section = 2;
        _ = sections.SelectedItem.Should().BeSameAs(sections.Items[2]);
        sections.SelectedItem = sections.Items[0];
        _ = model.Section.Should().Be(0);
        await model.ValidateCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        _ = model.StatusText.Should().Be("Output validated");
        _ = model.SelectedAsset!.Name.Should().Be("Red");
        pipeline.Verify(value => value.InspectCookedOutputAsync(scope.Scope, It.IsAny<CancellationToken>(), It.IsAny<bool>(), project), Times.Exactly(2));
        pipeline.VerifyNoOtherCalls();
    });

    /// <summary>A row names its own product and type, so the products of one model or input file stay apart.</summary>
    [TestMethod]
    public void RowsNameTheirOwnProductAndType()
    {
        var root = CreateInspectionReport(CreateQueryProject().ActiveProject!, validate: false).Roots[0];
        var model = AssetPath.Parse("/Content/Models/Lantern.glb");
        var input = AssetPath.Parse("/Content/Input/Player.input");

        var brass = new InspectionAssetRow(root, new("/Content/Models/Lantern.glb/Brass.omat", ContentCookAssetKind.Material), new(AssetPath.Parse("/Content/Models/Lantern.glb/Brass.omat"), model, []));
        var jump = new InspectionAssetRow(root, new("/Content/Input/Player.input/Jump.oiact", ContentCookAssetKind.Unknown), new(AssetPath.Parse("/Content/Input/Player.input/Jump.oiact"), input, []));
        var onFoot = new InspectionAssetRow(root, new("/Content/Input/Player.input/OnFoot.oimap", ContentCookAssetKind.Unknown), new(AssetPath.Parse("/Content/Input/Player.input/OnFoot.oimap"), input, []));

        _ = (brass.Name, brass.Kind).Should().Be(("Brass", AssetKind.Material));
        _ = (jump.Name, jump.Kind).Should().Be(("Jump", AssetKind.InputAction));
        _ = (onFoot.Name, onFoot.Kind).Should().Be(("OnFoot", AssetKind.InputMappingContext));
    }

    /// <summary>Superseded and disposed report requests drain without displaying late results or starting overlapping reads.</summary>
    /// <returns>The asynchronous panel-lifetime regression.</returns>
    [TestMethod]
    public Task InspectionDrainsSupersededAndDisposedRequests() => EnqueueAsync(async () =>
    {
        var projects = CreateQueryProject();
        var project = projects.ActiveProject!;
        var scope = new CookedInspectionScope(project, Scope: null, Validate: false);
        var first = new TaskCompletionSource<CookedOutputReport>(TaskCreationOptions.RunContinuationsAsynchronously);
        var second = new TaskCompletionSource<CookedOutputReport>(TaskCreationOptions.RunContinuationsAsynchronously);
        var calls = 0;
        var pipeline = new Mock<IContentPipelineService>();
        _ = pipeline.Setup(value => value.InspectCookedOutputAsync(It.Is<AssetPath?>(asset => asset == null), It.IsAny<CancellationToken>(), It.IsAny<bool>(), project)).Returns(() => ++calls == 1 ? first.Task : second.Task);
        var model = new CookedInspectionViewModel(pipeline.Object, NoAssets(), projects, _ => Task.FromResult(false));
        var original = model.InspectAsync(scope);
        var refresh = model.InspectAsync(scope with { Validate = true });
        _ = calls.Should().Be(1);
        first.SetResult(CreateInspectionReport(project, validate: false));
        await original.ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = calls.Should().Be(2);
        _ = model.Report.Should().BeNull();
        model.Dispose();
        _ = refresh.IsCompleted.Should().BeFalse();
        second.SetResult(CreateInspectionReport(project, validate: true));
        await refresh.ConfigureAwait(true);
        _ = model.Report.Should().BeNull();
        _ = model.IsBusy.Should().BeFalse();
        _ = model.InspectAsync(scope);
        _ = calls.Should().Be(2);
    });

    /// <summary>The panel starts with nothing inspected, and nothing to refresh or validate.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task InspectionStartsEmpty() => EnqueueAsync(() =>
    {
        var pipeline = new Mock<IContentPipelineService>(MockBehavior.Strict);
        using var model = new CookedInspectionViewModel(pipeline.Object, NoAssets(), CreateQueryProject(), _ => Task.FromResult(false));

        _ = model.Scope.Should().Be("Nothing inspected");
        _ = model.Report.Should().BeNull();
        _ = model.Assets.Should().BeEmpty();
        _ = model.RefreshCommand.CanExecute(parameter: null).Should().BeFalse();
        _ = model.ValidateCommand.CanExecute(parameter: null).Should().BeFalse();
        pipeline.VerifyNoOtherCalls();
        return Task.CompletedTask;
    });

    /// <summary>A scope replaces the shown one, and a repeated request for the shown scope inspects it again.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task InspectionTakesAScopeAndRefreshesOnARepeatedRequest() => EnqueueAsync(async () =>
    {
        var projects = CreateQueryProject();
        var project = projects.ActiveProject!;
        var materials = new CookedInspectionScope(project, AssetPath.Parse("/Content/Materials"), Validate: false);
        var scenes = new CookedInspectionScope(project, AssetPath.Parse("/Content/Scenes"), Validate: false);
        var pipeline = new Mock<IContentPipelineService>();
        _ = pipeline.Setup(value => value.InspectCookedOutputAsync(It.IsAny<AssetPath?>(), It.IsAny<CancellationToken>(), It.IsAny<bool>(), project)).Returns((AssetPath? _, CancellationToken _, bool validate, ProjectContext? _) => Task.FromResult(CreateInspectionReport(project, validate)));
        using var model = new CookedInspectionViewModel(pipeline.Object, NoAssets(), projects, _ => Task.FromResult(false));

        await model.InspectAsync(materials).ConfigureAwait(true);
        _ = model.Scope.Should().EndWith("/Content/Materials");
        var report = model.Report;
        _ = report.Should().NotBeNull();
        await model.InspectAsync(materials with { Validate = true }).ConfigureAwait(true);
        _ = model.Report.Should().NotBeSameAs(report, "a repeated request inspects the shown scope again");
        _ = model.StatusText.Should().Be("Output validated");
        await model.InspectAsync(scenes).ConfigureAwait(true);
        _ = model.Scope.Should().EndWith("/Content/Scenes");

        pipeline.Verify(value => value.InspectCookedOutputAsync(materials.Scope, It.IsAny<CancellationToken>(), It.IsAny<bool>(), project), Times.Exactly(2));
        pipeline.Verify(value => value.InspectCookedOutputAsync(scenes.Scope, It.IsAny<CancellationToken>(), It.IsAny<bool>(), project), Times.Once);
    });

    private static IAssetFeed NoAssets() => Mock.Of<IAssetFeed>(feed => feed.Items == Observable.Never<IReadOnlyList<AssetEntry>>());

    private static CookedOutputReport CreateInspectionReport(ProjectContext project, bool validate)
    {
        var root = project.ProjectRoot.Combine(".cooked").Combine("Content").FullPath;
        var red = AssetPath.Parse("/Content/Materials/Red.omat");
        var main = AssetPath.Parse("/Content/Scenes/Main.oscene");
        var assets = new[]
        {
            new CookedAssetEntry("/Content/Scenes/Main.oscene", ContentCookAssetKind.Scene),
            new CookedAssetEntry("/Content/Materials/Red.omat", ContentCookAssetKind.Material),
        };
        CookedFileEntry[] files = [new("container.index.bin", 1024), new("Scenes/Main.oscene", 256), new("Materials/Red.omat", 128)];
        var inspection = new CookInspectionResult(root, Succeeded: true, SourceIdentity: Guid.NewGuid(), assets, files, Diagnostics: []);
        CookedAssetProvenance[] origins = [new(main, main, [red]), new(red, red, []),];
        var rootReport = new CookedRootReport("Content", IsPresent: true, inspection, validate ? new(root, Succeeded: true, []) : null, origins);
        return new(project.ProjectId, ScopeUri: null, DateTimeOffset.UtcNow, [rootReport]);
    }
}
