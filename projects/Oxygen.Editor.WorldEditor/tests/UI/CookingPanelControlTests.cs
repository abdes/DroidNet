// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices.WindowsRuntime;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Docking;
using DroidNet.Docking.Controls;
using DroidNet.Docking.Workspace;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Moq;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Cooking;
using Oxygen.Managed.Core.Diagnostics;
using Windows.Foundation;
using Windows.Graphics.Imaging;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises the real cooking panel's compact layout, native feedback, and scrolling.</summary>
[TestClass]
[TestCategory("UITest")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class CookingPanelControlTests : VisualUserInterfaceTests
{
    private Windows.Graphics.SizeInt32 originalWindowSize;

    /// <summary>Gets or sets the active test context and artifact directory.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Wide and narrow docks keep controls scoped and expanded content reachable.</summary>
    /// <param name="width">The dock width in device-independent pixels.</param>
    /// <param name="theme">The WinUI theme to render.</param>
    /// <returns>The asynchronous UI test.</returns>
    [TestMethod]
    [DataRow(960d, ElementTheme.Dark)]
    [DataRow(360d, ElementTheme.Light)]
    public Task CookingLayoutKeepsRecoveryAndOutputReachable(double width, ElementTheme theme) => EnqueueAsync(async () =>
    {
        using var model = CreateModel();
        model.SelectedRun!.IsAssetsExpanded = true;
        var view = new CookingPanelView { ViewModel = model, Width = width, Height = 340, RequestedTheme = theme };
        var host = (Border)XamlReader.Load("<Border xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' Background='{ThemeResource ApplicationPageBackgroundThemeBrush}' />");
        host.RequestedTheme = theme;
        host.Width = width;
        host.Height = 340;
        host.Child = view;
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var scale = view.XamlRoot.RasterizationScale;
        VisualUserInterfaceTestsApp.MainWindow.AppWindow.Resize(new Windows.Graphics.SizeInt32((int)(width * scale) + 40, (int)(340 * scale) + 80));
        await WaitForRenderAsync().ConfigureAwait(true);
        view.UpdateLayout();
        await this.CaptureAsync(host, string.Create(System.Globalization.CultureInfo.InvariantCulture, $"cooking-{width}-{theme}.png")).ConfigureAwait(true);

        var toolbar = view.FindDescendant<DroidNet.Controls.ToolBar>()!;
        _ = toolbar.IsCompact.Should().BeTrue();
        _ = toolbar.Name.Should().Be("RunListToolbar");
        var showAll = view.FindDescendant<DroidNet.Controls.ToolBarToggleButton>()!;
        _ = showAll.Label.Should().Be("Show all");
        showAll.IsChecked = true;
        _ = model.ShowAll.Should().BeTrue();
        _ = view.FindDescendants().OfType<Button>().Should().NotContain(button => Equals(button.Content, "Cook project"));

        AssertCompactHeader(view);

        var output = view.FindDescendant<DroidNet.Controls.OutputConsole.OutputConsoleView>()!;
        var assets = view.FindDescendant<ListView>(element => string.Equals(element.Name, "CookAssets", StringComparison.Ordinal))!;
        _ = output.ActualHeight.Should().BeGreaterThanOrEqualTo(160);
        _ = assets.ActualHeight.Should().BeGreaterThanOrEqualTo(120);
        _ = output.IsScrollEnabled.Should().BeFalse();
        _ = ScrollViewer.GetVerticalScrollBarVisibility(assets).Should().Be(ScrollBarVisibility.Disabled);
        _ = ScrollViewer.GetVerticalScrollMode(assets).Should().Be(ScrollMode.Disabled);
        _ = assets.Items.Should().HaveCount(model.SelectedRun.Assets.Count).And.NotBeEmpty();
        var scroller = view.FindDescendant<ScrollViewer>(element => string.Equals(element.Name, "DetailsScroller", StringComparison.Ordinal))!;
        _ = scroller.ScrollableHeight.Should().BePositive();
        _ = view.FindDescendants().OfType<Expander>().Should().Contain(element => Equals(element.Header, "Issues · Main (1)"));
        _ = view.FindDescendants().OfType<Expander>().Should().Contain(element => Equals(element.Header, "Issues · OtherScene (1)"));
        var issue = view.FindDescendant<InfoBar>(element => string.Equals(element.Message, "Aerial Start must be 0 m or greater.", StringComparison.Ordinal))!;
        _ = issue.Severity.Should().Be(InfoBarSeverity.Error);
        _ = issue.ActionButton.Should().BeOfType<HyperlinkButton>();
        _ = issue.ActionButton.Visibility.Should().Be(Visibility.Visible);

        AssertResponsiveLayout(view, toolbar, issue, scroller, width);
    });

    /// <summary>Failure icons use the platform's semantic critical brush in both themes.</summary>
    /// <param name="theme">The theme to resolve.</param>
    /// <returns>The asynchronous UI test.</returns>
    [TestMethod]
    [DataRow(ElementTheme.Dark)]
    [DataRow(ElementTheme.Light)]
    public Task FailedStatusUsesWinUiCriticalColour(ElementTheme theme) => EnqueueAsync(async () =>
    {
        var reference = (Border)XamlReader.Load("<Border xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' Background='{ThemeResource SystemFillColorCriticalBrush}' Width='1' Height='1' />");
        var status = new CookingStatusIndicator { State = CookRunState.Failed };
        var root = new StackPanel { RequestedTheme = theme, Children = { reference, status } };
        await LoadTestContentAsync(root).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = status.ActualTheme.Should().Be(theme);
        var icon = status.FindDescendant<FontIcon>()!;
        _ = ((SolidColorBrush)icon.Foreground).Color.Should().Be(((SolidColorBrush)reference.Background).Color);
    });

    /// <summary>Unsaved recovery stays compact and a document's name opens that document.</summary>
    /// <returns>The asynchronous layout and navigation regression.</returns>
    [TestMethod]
    public Task UnsavedRecoveryKeepsItsActionInlineAndNamesNavigable() => EnqueueAsync(async () =>
    {
        var document = new Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentState(Guid.NewGuid(), "C:\\Project\\Content\\NewScene2.oscene.json", "NewScene2", Revision: 2, SavedRevision: 1, IsDirty: true, SavedContentHash: string.Empty);
        var actions = new Mock<ICookingWorkspaceActions>();
        _ = actions.Setup(value => value.OpenDocumentAsync(document.DocumentId)).ReturnsAsync(value: true);
        using var model = CreateModel(actions.Object, document);
        var view = new CookingPanelView { ViewModel = model, Width = 960, Height = 340, RequestedTheme = ElementTheme.Dark };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var scale = view.XamlRoot.RasterizationScale;
        VisualUserInterfaceTestsApp.MainWindow.AppWindow.Resize(new Windows.Graphics.SizeInt32((int)(960 * scale) + 40, (int)(340 * scale) + 80));
        await WaitForRenderAsync().ConfigureAwait(true);
        view.UpdateLayout();
        var banner = view.FindDescendant<InfoBar>(value => string.Equals(value.Title, "Unsaved documents", StringComparison.Ordinal))!;
        var title = banner.FindDescendant<TextBlock>(value => string.Equals(value.Name, "Title", StringComparison.Ordinal))!;
        var action = banner.ActionButton;
        _ = Math.Abs(title.TransformToVisual(view).TransformPoint(default).Y - action.TransformToVisual(view).TransformPoint(default).Y).Should().BeLessThan(20);
        _ = (banner.TransformToVisual(view).TransformPoint(default).Y + banner.ActualHeight).Should().BeLessThan(view.ActualHeight);
        var link = banner.FindDescendant<HyperlinkButton>()!;
        _ = ((TextBlock)link.Content).Text.Should().Be(document.DisplayName);
        ((Microsoft.UI.Xaml.Automation.Provider.IInvokeProvider)new Microsoft.UI.Xaml.Automation.Peers.HyperlinkButtonAutomationPeer(link).GetPattern(Microsoft.UI.Xaml.Automation.Peers.PatternInterface.Invoke)).Invoke();
        await WaitForRenderAsync().ConfigureAwait(true);
        actions.Verify(value => value.OpenDocumentAsync(document.DocumentId), Times.Once);
        await this.CaptureAsync(view, "cooking-unsaved-inline.png").ConfigureAwait(true);
    });

    /// <summary>Recovery activation waits for the initiating menu event to return and selects the requested run.</summary>
    /// <returns>The asynchronous attention regression.</returns>
    [TestMethod]
    public Task RequestedAttentionSelectsTheRunAfterTheInitiatingEvent() => EnqueueAsync(async () =>
    {
        var runs = new Mock<ICookRunService>();
        using var model = CreateModel(runService: runs);
        var next = model.SelectedRun!.Snapshot with { OperationId = Guid.NewGuid(), State = CookRunState.NeedsSave, CompletedAt = null };
        var revealed = new TaskCompletionSource<Guid>(TaskCreationOptions.RunContinuationsAsynchronously);
        model.RevealRequested += (_, _) => revealed.TrySetResult(model.SelectedRun!.Snapshot.OperationId);
        runs.Raise(value => value.RunChanged += null, new CookRunChangedEventArgs(next, reveal: true));
        _ = revealed.Task.IsCompleted.Should().BeFalse();
        _ = (await revealed.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true)).Should().Be(next.OperationId);
    });

    /// <summary>Cook attention changes the actual dock tab and visible content, including after returning to the browser.</summary>
    /// <returns>The asynchronous dock activation regression.</returns>
    [TestMethod]
    public Task CookingAttentionActivatesTheRenderedDockAtStartAndCompletion() => EnqueueAsync(async () =>
    {
        var runs = new Mock<ICookRunService>();
        using var model = CreateModel(runService: runs);
        using var docker = new Docker();
        var dock = ToolDock.New();
        var browser = Dockable.New(Guid.NewGuid().ToString("N"));
        var cooking = Dockable.New(Guid.NewGuid().ToString("N"));
        var browserView = new TextBlock { Text = "Content Browser" };
        var cookingView = new CookingPanelView { ViewModel = model };
        browser.ViewModel = browserView;
        cooking.ViewModel = cookingView;
        dock.AdoptDockable(browser);
        dock.AdoptDockable(cooking);
        docker.Dock(dock, new AnchorBottom());
        browser.IsActive = true;
        var converter = new Mock<Microsoft.UI.Xaml.Data.IValueConverter>();
        _ = converter.Setup(value => value.Convert(It.IsAny<object>(), It.IsAny<Type>(), It.IsAny<object>(), It.IsAny<string>()))
            .Returns((object value, Type _, object _, string _) => value);
        var resources = Application.Current.Resources;
        var hadConverter = resources.TryGetValue("VmToViewConverter", out var previousConverter);
        resources["VmToViewConverter"] = converter.Object;
        var dockModel = new DockPanelViewModel(dock);
        try
        {
            var panel = new DockPanel { VmToViewConverter = converter.Object, ViewModel = dockModel, Width = 960, Height = 340 };
            model.RevealRequested += (_, _) =>
            {
                docker.PinDock(dock);
                cooking.IsActive = true;
            };
            await LoadTestContentAsync(panel).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            var tabs = panel.FindDescendant<DockableTabsBar>()!;
            _ = tabs.ActiveDockable.Should().Be(browser);
            _ = panel.FindDescendants().Should().Contain(browserView);

            await VerifyDockAttentionAsync(runs, model, dock, browser, cooking, browserView, cookingView, panel, tabs).ConfigureAwait(true);

            await this.CaptureAsync(panel, "cooking-dock-activated.png").ConfigureAwait(true);
        }
        finally
        {
            dockModel.IsActive = false;
            if (hadConverter)
            {
                resources["VmToViewConverter"] = previousConverter;
            }
            else
            {
                _ = resources.Remove("VmToViewConverter");
            }
        }
    });

    /// <summary>Long Output and Assets sections grow past the former caps and remain reachable through the parent.</summary>
    /// <returns>The asynchronous UI test.</returns>
    [TestMethod]
    public Task ExpandedSectionsGrowToTheirCompleteContents() => EnqueueAsync(async () =>
    {
        using var model = CreateModel();
        var run = model.SelectedRun!;
        var snapshot = run.Snapshot;
        var assets = snapshot.Assets;
        for (var index = 0; index < 40; index++)
        {
            var uri = new Uri(string.Create(System.Globalization.CultureInfo.InvariantCulture, $"asset:///Content/Materials/Material{index}.omat.json"));
            assets = assets.Add(uri, new(uri, ContentCookAssetKind.Material, CookAssetState.Skipped));
        }

        run.Apply(snapshot with
        {
            Revision = snapshot.Revision + 1,
            Assets = assets,
            Messages = snapshot.Messages.AddRange(Enumerable.Range(0, 40).Select(index => new CookRunMessage(index + 3, DateTimeOffset.UtcNow, DiagnosticSeverity.Info, string.Create(System.Globalization.CultureInfo.InvariantCulture, $"Checking asset {index}.")))),
        });
        run.IsAssetsExpanded = true;
        var view = new CookingPanelView { ViewModel = model, Width = 700, Height = 340 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        view.UpdateLayout();
        var output = view.FindDescendant<DroidNet.Controls.OutputConsole.OutputConsoleView>()!;
        var list = view.FindDescendant<ListView>(element => string.Equals(element.Name, "CookAssets", StringComparison.Ordinal))!;
        var scroller = view.FindDescendant<ScrollViewer>(element => string.Equals(element.Name, "DetailsScroller", StringComparison.Ordinal))!;
        _ = output.ActualHeight.Should().BeGreaterThan(360);
        _ = list.ActualHeight.Should().BeGreaterThan(240);
        _ = output.FindDescendant<ScrollViewer>()!.ScrollableHeight.Should().Be(0);
        _ = list.FindDescendant<ScrollViewer>()!.ScrollableHeight.Should().Be(0);
        _ = scroller.ChangeView(horizontalOffset: null, scroller.ScrollableHeight, zoomFactor: null, disableAnimation: true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = scroller.VerticalOffset.Should().BeGreaterThan(360);
    });

    /// <inheritdoc />
    protected override void TestSetup() => this.originalWindowSize = VisualUserInterfaceTestsApp.MainWindow.AppWindow.Size;

    /// <inheritdoc />
    protected override async Task TestCleanupAsync()
    {
        VisualUserInterfaceTestsApp.MainWindow.AppWindow.Resize(this.originalWindowSize);
        await WaitForRenderAsync().ConfigureAwait(true);
    }

    private static async Task VerifyDockAttentionAsync(Mock<ICookRunService> runs, CookingPanelViewModel model, ToolDock dock, Dockable browser, Dockable cooking, TextBlock browserView, CookingPanelView cookingView, DockPanel panel, DockableTabsBar tabs)
    {
        foreach (var state in new[] { CookRunState.Queued, CookRunState.NeedsSave, CookRunState.Succeeded })
        {
            tabs.ActiveDockable = browser;
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = dock.ActiveDockable.Should().Be(browser);
            var next = model.SelectedRun!.Snapshot with { State = state, Revision = model.SelectedRun.Snapshot.Revision + 1 };
            runs.Raise(value => value.RunChanged += null, new CookRunChangedEventArgs(next, reveal: true));
            await WaitForRenderAsync().ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = tabs.ActiveDockable.Should().Be(cooking);
            _ = panel.FindDescendants().Should().Contain(cookingView).And.NotContain(browserView);
            _ = dock.Dockables.Count(value => value.IsActive).Should().Be(1);
        }
    }

    private static void AssertCompactHeader(CookingPanelView view)
    {
        var title = view.FindDescendant<TextBlock>(element => string.Equals(element.Name, "RunTitle", StringComparison.Ordinal))!;
        _ = title.Text.Should().Be("Vortex (Project)");
        var pill = view.FindDescendant<CookingStatusIndicator>(element => string.Equals(element.Name, "RunStatus", StringComparison.Ordinal))!;
        var retry = view.FindDescendant<Button>(element => string.Equals(element.Name, "RetryButton", StringComparison.Ordinal))!;
        var pillPoint = pill.TransformToVisual(view).TransformPoint(default);
        var retryPoint = retry.TransformToVisual(view).TransformPoint(default);
        _ = Math.Abs(pillPoint.Y - retryPoint.Y).Should().BeLessThan(10);
        _ = (retryPoint.X - pillPoint.X - pill.ActualWidth).Should().BeInRange(0, 16);
    }

    private static void AssertResponsiveLayout(CookingPanelView view, DroidNet.Controls.ToolBar toolbar, InfoBar issue, ScrollViewer scroller, double width)
    {
        if (width > 560)
        {
            var list = view.FindDescendant<ListView>(element => string.Equals(element.Name, "RunList", StringComparison.Ordinal))!;
            _ = toolbar.ActualWidth.Should().BeLessThanOrEqualTo(list.ActualWidth);
            var message = issue.FindDescendant<TextBlock>(element => string.Equals(element.Name, "Message", StringComparison.Ordinal))!;
            _ = message.Should().NotBeNull();
            var messagePoint = message.TransformToVisual(issue).TransformPoint(default);
            var actionPoint = issue.ActionButton.TransformToVisual(issue).TransformPoint(default);
            _ = Math.Abs(actionPoint.Y - messagePoint.Y).Should().BeLessThan(20);
        }
        else
        {
            _ = view.FindDescendant<ComboBox>(element => string.Equals(element.Name, "NarrowRunPicker", StringComparison.Ordinal))!.Visibility.Should().Be(Visibility.Visible);
            _ = scroller.ExtentWidth.Should().BeLessThanOrEqualTo(scroller.ViewportWidth + 1);
        }
    }

    private static CookingPanelViewModel CreateModel(ICookingWorkspaceActions? actions = null, Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentState? waitingDocument = null, Mock<ICookRunService>? runService = null)
    {
        var projects = new ProjectContextService();
        var projectId = Guid.NewGuid();
        projects.Activate(new ProjectContext
        {
            ProjectId = projectId, Name = "Vortex", Category = Category.Games, ProjectRoot = Path.GetTempPath(),
            AuthoringMounts = [], LocalFolderMounts = [], Scenes = [],
        });
        var operationId = Guid.NewGuid();
        DiagnosticRecord Issue(string name, string message) => new()
        {
            OperationId = operationId, Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error,
            Code = "UI.Cook.InvalidInput", Message = message, AffectedVirtualPath = $"/Content/Scenes/{name}.oscene.json",
            AffectedEntity = new AffectedScope { SceneName = name },
            SuggestedAction = new PrimaryAction { ActionId = "Cook.GoToProperty", Label = "Go to property", Kind = PrimaryActionKind.Custom },
        };
        var run = new CookRunSnapshot
        {
            OperationId = operationId, ProjectId = projectId, ProjectRoot = Path.GetTempPath(), DisplayName = "Vortex",
            Request = new(CookTargetKind.Project, null), State = CookRunState.Failed, CompletedAt = DateTimeOffset.UtcNow,
            Diagnostics = [Issue("Main", "Aerial Start must be 0 m or greater."), Issue("OtherScene", "A referenced geometry could not be loaded.")],
        };
        var mainUri = new Uri("asset:///Content/Scenes/Main.oscene.json");
        run = run with
        {
            Assets = run.Assets.Add(mainUri, new(mainUri, ContentCookAssetKind.Scene, CookAssetState.Failed)),
            Messages = run.Messages.Add(new(1, DateTimeOffset.UtcNow, DiagnosticSeverity.Info, "Checking saved inputs."))
                .Add(new(2, DateTimeOffset.UtcNow, DiagnosticSeverity.Error, "Aerial Start must be 0 m or greater.")),
        };
        if (waitingDocument is not null)
        {
            run = run with { State = CookRunState.NeedsSave, CompletedAt = null, Diagnostics = [], UnsavedDocuments = [waitingDocument], Assets = run.Assets.Clear(), Messages = [] };
        }

        var runs = runService ?? new Mock<ICookRunService>();
        _ = runs.SetupGet(service => service.Runs).Returns(new[] { run });
        var dispatcher = DispatcherQueue.GetForCurrentThread();
        var hosting = new HostingContext { Application = Application.Current, Dispatcher = dispatcher, DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher) };
        return new(runs.Object, Mock.Of<IContentPipelineService>(), projects, Mock.Of<IProjectAssetCatalog>(), actions ?? Mock.Of<ICookingWorkspaceActions>(), new StrongReferenceMessenger(), hosting);
    }

    private async Task CaptureAsync(FrameworkElement view, string name)
    {
        // Capture after native Expander and InfoBar entrance transitions finish.
        await Task.Delay(350, this.TestContext.CancellationToken).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var bitmap = new RenderTargetBitmap();
        await bitmap.RenderAsync(view);
        var pixels = (await bitmap.GetPixelsAsync()).ToArray();
        var directory = Path.Combine(Path.GetTempPath(), "oxygen-cooking-layout", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, name);
        var file = File.Create(path);
        await using var fileLifetime = file.ConfigureAwait(false);
        using var stream = file.AsRandomAccessStream();
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, stream);
        encoder.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied, (uint)bitmap.PixelWidth, (uint)bitmap.PixelHeight, 96, 96, pixels);
        await encoder.FlushAsync();
        this.TestContext.AddResultFile(path);
    }
}
