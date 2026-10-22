// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Docking.Controls;
using DroidNet.Docking.Workspace;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Docking.Tests;

/// <summary>
/// A dock panel shows one view per dockable view model, reused for as long as the dockable stays in its dock.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DockPanel")]
[TestCategory("UITest")]
public sealed partial class DockPanelViewHostingTests : VisualUserInterfaceTests, IDisposable
{
    private readonly Docker docker = new();
    private readonly ResolvedViews resolved = new();
    private readonly object browserViewModel = new();
    private readonly object logsViewModel = new();
    private Dockable browser = null!;
    private Dockable logs = null!;
    private IDock dock = null!;

    public void Dispose() => this.docker.Dispose();

    [TestMethod]
    public Task SwitchingTabs_ReusesTheViewOfEachDockable_Async() => EnqueueAsync(
        async () =>
        {
            var panel = this.CreatePanel();
            await LoadTestContentAsync(panel).ConfigureAwait(true);

            for (var round = 0; round < 3; round++)
            {
                this.logs.IsActive = true;
                this.browser.IsActive = true;
            }

            await WaitForRenderAsync().ConfigureAwait(true);

            _ = this.resolved.Views.Select(view => view.ViewModel).Should().Equal(this.browserViewModel, this.logsViewModel);
            _ = this.resolved.Views[0].IsLoaded.Should().BeTrue();
            _ = this.resolved.Views[1].IsLoaded.Should().BeFalse();
        });

    [TestMethod]
    public Task ClosingADockable_ReleasesItsView_Async() => EnqueueAsync(
        async () =>
        {
            var panel = this.CreatePanel();
            await LoadTestContentAsync(panel).ConfigureAwait(true);
            this.logs.IsActive = true;
            this.browser.IsActive = true;
            _ = this.resolved.Views.Should().HaveCount(2);

            this.dock.DisownDockable(this.logs);

            _ = this.resolved.Views[1].ViewModel.Should().BeNull();
            _ = this.resolved.Views[0].ViewModel.Should().BeSameAs(this.browserViewModel);
        });

    /// <inheritdoc/>
    protected override void TestSetup()
    {
        this.dock = this.docker.CreateDock<ToolDock>();
        this.browser = this.docker.CreateDockable<Dockable>("browser");
        this.browser.ViewModel = this.browserViewModel;
        this.logs = this.docker.CreateDockable<Dockable>("logs");
        this.logs.ViewModel = this.logsViewModel;
        this.dock.AdoptDockable(this.browser);
        this.dock.AdoptDockable(this.logs);
        var anchor = new AnchorBottom();
        try
        {
            this.docker.Dock(this.dock, anchor);
            anchor = null;
        }
        finally
        {
            anchor?.Dispose();
        }

        this.browser.IsActive = true;
    }

    private DockPanel CreatePanel() => new()
    {
        Width = 400,
        Height = 300,
        VmToViewConverter = new ViewModelToView(this.resolved),
        ViewModel = new DockPanelViewModel(this.dock),
    };

    /// <summary>Resolves a new view for every request and keeps them in creation order.</summary>
    private sealed class ResolvedViews : IViewLocator
    {
        public List<TestView> Views { get; } = [];

        public object? ResolveView(object viewModel)
        {
            var view = new TestView();
            this.Views.Add(view);
            return view;
        }

        public IViewFor<T>? ResolveView<T>()
            where T : class
            => throw new InvalidOperationException();
    }

    private sealed partial class TestView : UserControl, IViewFor
    {
        public object? ViewModel { get; set; }
    }
}
