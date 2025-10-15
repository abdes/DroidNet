// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Moq;

namespace DroidNet.Controls.Menus.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("ContextMenuTests")]
public sealed class ContextMenuTests : VisualUserInterfaceTests
{
    private Func<IRootMenuSurface?, ICascadedMenuHost>? originalFactory;

    [TestInitialize]
    public void InitializeFactory() => this.originalFactory = ContextMenu.CreateMenuHost;

    [TestCleanup]
    public void RestoreFactory() => ContextMenu.CreateMenuHost = this.originalFactory!;

    [TestMethod]
    public Task SetMenuSource_FirstAssignmentCreatesHostWithAdapter() => EnqueueAsync(() =>
    {
        var trigger = new Button();
        var menuSource = CreateMenuSource("Open");
        var harness = new HostHarness();

        ContextMenu.CreateMenuHost = harness.InitializeHost;

        ContextMenu.SetMenuSource(trigger, menuSource);

        _ = harness.LastFactoryArgument.Should().NotBeNull();
        _ = harness.LastFactoryArgument!.GetType().Name.Should().Contain("ContextMenuRootSurfaceAdapter");
        _ = harness.HostMock.Object.RootSurface.Should().BeSameAs(harness.LastFactoryArgument);
        _ = harness.Events.OpenedHandlerCount.Should().Be(1);
        _ = harness.Events.ClosedHandlerCount.Should().Be(1);

        var storedHost = GetMenuHost(trigger);
        _ = storedHost.Should().BeSameAs(harness.HostMock.Object);

        return Task.CompletedTask;
    });

    [TestMethod]
    public Task SetMenuSource_ReassignmentDisposesPreviousHost() => EnqueueAsync(() =>
    {
        var trigger = new Button();
        var firstHarness = new HostHarness();
        var secondHarness = new HostHarness();
        var hosts = new Queue<HostHarness>([firstHarness, secondHarness]);

        ContextMenu.CreateMenuHost = rootSurface => hosts.Dequeue().InitializeHost(rootSurface);

        var firstSource = CreateMenuSource("File");
        ContextMenu.SetMenuSource(trigger, firstSource);

        var secondSource = CreateMenuSource("Edit");
        ContextMenu.SetMenuSource(trigger, secondSource);

        _ = firstHarness.Events.Disposed.Should().BeTrue();
        _ = firstHarness.Events.OpenedHandlerCount.Should().Be(0);
        _ = firstHarness.Events.ClosedHandlerCount.Should().Be(0);

        _ = secondHarness.LastFactoryArgument.Should().NotBeNull();
        _ = secondHarness.Events.Disposed.Should().BeFalse();

        var storedHost = GetMenuHost(trigger);
        _ = storedHost.Should().BeSameAs(secondHarness.HostMock.Object);

        return Task.CompletedTask;
    });

    [TestMethod]
    public Task SetMenuSource_ClearingDetachesAndDisposesHost() => EnqueueAsync(() =>
    {
        var trigger = new Button();
        var harness = new HostHarness();

        ContextMenu.CreateMenuHost = harness.InitializeHost;

        ContextMenu.SetMenuSource(trigger, CreateMenuSource("View"));
        ContextMenu.SetMenuSource(trigger, value: null);

        _ = harness.Events.Disposed.Should().BeTrue();
        _ = harness.Events.OpenedHandlerCount.Should().Be(0);
        _ = harness.Events.ClosedHandlerCount.Should().Be(0);
        _ = GetMenuHost(trigger).Should().BeNull();

        InvokeContextRequested(trigger, CreateContextRequestedEventArgs());
        _ = harness.Events.ShowAtCalls.Should().Be(0);

        return Task.CompletedTask;
    });

    [TestMethod]
    public Task ElementUnloaded_DisposesHostAndClearsAttachment() => EnqueueAsync(() =>
    {
        var trigger = new Button();
        var harness = new HostHarness();

        ContextMenu.CreateMenuHost = harness.InitializeHost;

        ContextMenu.SetMenuSource(trigger, CreateMenuSource("Tools"));

        InvokeElementUnloaded(trigger);

        _ = harness.Events.Disposed.Should().BeTrue();
        _ = GetMenuHost(trigger).Should().BeNull();
        _ = harness.Events.OpenedHandlerCount.Should().Be(0);
        _ = harness.Events.ClosedHandlerCount.Should().Be(0);

        return Task.CompletedTask;
    });

    [TestMethod]
    public Task ContextRequested_ShowsMenuUsingHost() => EnqueueAsync(() =>
    {
        var trigger = new Button();
        var harness = new HostHarness();
        var menuSource = CreateMenuSource("Open", "Close");

        ContextMenu.CreateMenuHost = harness.InitializeHost;

        ContextMenu.SetMenuSource(trigger, menuSource);
        var initialAdapter = harness.HostMock.Object.RootSurface;
        _ = initialAdapter.Should().NotBeNull();

        var args = CreateContextRequestedEventArgs();
        InvokeContextRequested(trigger, args);

        _ = args.Handled.Should().BeTrue();
        _ = harness.Events.ShowAtCalls.Should().Be(1);
        _ = harness.Events.Anchor.Should().Be(trigger);
        _ = harness.Events.LastNavigationMode.Should().Be(MenuNavigationMode.PointerInput);

        var appliedSource = harness.HostMock.Object.MenuSource.Should().BeOfType<MenuSourceView>().Which;
        _ = appliedSource.Services.Should().BeSameAs(menuSource.Services);
        _ = appliedSource.Items.Should().HaveCount(menuSource.Items.Count);

        return Task.CompletedTask;
    });

    [TestMethod]
    public Task ContextRequested_RefreshesPendingSourceOnSubsequentInvocation() => EnqueueAsync(() =>
    {
        var trigger = new Button();
        var harness = new HostHarness();
        var menuSource = CreateMenuSource("Main");

        ContextMenu.CreateMenuHost = harness.InitializeHost;

        ContextMenu.SetMenuSource(trigger, menuSource);

        InvokeContextRequested(trigger, CreateContextRequestedEventArgs());
        var firstView = harness.HostMock.Object.MenuSource.Should().BeOfType<MenuSourceView>().Which;

        menuSource.Items.Add(new MenuItemData { Text = "Secondary" });
        InvokeContextRequested(trigger, CreateContextRequestedEventArgs());

        var secondView = harness.HostMock.Object.MenuSource.Should().BeOfType<MenuSourceView>().Which;
        _ = secondView.Should().NotBeSameAs(firstView);
        _ = secondView.Items.Should().ContainSingle(item => item.Text == "Secondary");

        return Task.CompletedTask;
    });

    [TestMethod]
    public Task ContextMenuRootSurfaceAdapter_DismissDelegatesToHost() => EnqueueAsync(() =>
    {
        var trigger = new Button();
        var harness = new HostHarness();
        var menuSource = CreateMenuSource("Action");

        ContextMenu.CreateMenuHost = harness.InitializeHost;

        ContextMenu.SetMenuSource(trigger, menuSource);
        InvokeContextRequested(trigger, CreateContextRequestedEventArgs());

        _ = harness.Events.IsOpen.Should().BeTrue();
        _ = harness.HostMock.Object.RootSurface.Should().NotBeNull();
        harness.HostMock.Object.RootSurface!.Dismiss(MenuDismissKind.KeyboardInput);

        _ = harness.Events.IsOpen.Should().BeFalse();

        return Task.CompletedTask;
    });

    private static IMenuSource CreateMenuSource(params string[] items)
    {
        var builder = new MenuBuilder();
        foreach (var item in items)
        {
            _ = builder.AddMenuItem(item);
        }

        return builder.Build();
    }

    private static ContextRequestedEventArgs CreateContextRequestedEventArgs() => new();

    private static ICascadedMenuHost? GetMenuHost(UIElement element)
    {
        var property = (DependencyProperty)typeof(ContextMenu)
            .GetField("MenuHostProperty", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)!
            .GetValue(null)!;
        return (ICascadedMenuHost?)element.GetValue(property);
    }

    private static void InvokeContextRequested(UIElement element, ContextRequestedEventArgs args)
        => _ = typeof(ContextMenu)
            .GetMethod("OnElementContextRequested", BindingFlags.NonPublic | BindingFlags.Static)!
            .Invoke(null, [element, args]);

    private static void InvokeElementUnloaded(UIElement element)
        => _ = typeof(ContextMenu)
            .GetMethod("OnElementUnloaded", BindingFlags.NonPublic | BindingFlags.Static)!
            .Invoke(null, [element, new RoutedEventArgs()]);

    private sealed class HostHarness
    {
        public HostHarness()
        {
            this.Events = new HostEvents();
            this.SurfaceMock = new Mock<ICascadedMenuSurface>(MockBehavior.Loose);
            _ = this.SurfaceMock.Setup(surface => surface.Dismiss(It.IsAny<MenuDismissKind>()));
            _ = this.SurfaceMock.Setup(surface => surface.FocusFirstItem(It.IsAny<MenuLevel>(), It.IsAny<MenuNavigationMode>())).Returns(value: true);

            var rootElement = new Grid();

            this.HostMock = new Mock<ICascadedMenuHost>(MockBehavior.Strict);
            _ = this.HostMock.SetupProperty(host => host.RootSurface);
            _ = this.HostMock.SetupProperty(host => host.MenuSource);
            _ = this.HostMock.SetupProperty(host => host.MaxLevelHeight);
            _ = this.HostMock.SetupGet(host => host.Surface).Returns(this.SurfaceMock.Object);
            _ = this.HostMock.SetupGet(host => host.RootElement).Returns(rootElement);
            _ = this.HostMock.SetupGet(host => host.Anchor).Returns(() => this.Events.Anchor);
            _ = this.HostMock.SetupGet(host => host.IsOpen).Returns(() => this.Events.IsOpen);

            _ = this.HostMock.Setup(host => host.ShowAt(It.IsAny<FrameworkElement>(), It.IsAny<MenuNavigationMode>()))
                .Callback<FrameworkElement, MenuNavigationMode>((anchor, mode) =>
                {
                    this.Events.IsOpen = true;
                    this.Events.Anchor = anchor;
                    this.Events.LastNavigationMode = mode;
                })
                .Returns(value: true);

            _ = this.HostMock.Setup(host => host.ShowAt(It.IsAny<FrameworkElement>(), It.IsAny<Windows.Foundation.Point>(), It.IsAny<MenuNavigationMode>()))
                .Callback<FrameworkElement, Windows.Foundation.Point, MenuNavigationMode>((anchor, point, mode) =>
                {
                    this.Events.IsOpen = true;
                    this.Events.Anchor = anchor;
                    this.Events.LastNavigationMode = mode;
                    this.Events.LastPosition = point;
                    this.Events.ShowAtCalls++;
                })
                .Returns(value: true);

            _ = this.HostMock.Setup(host => host.Dismiss(It.IsAny<MenuDismissKind>()))
                .Callback(() => this.Events.IsOpen = false);

            _ = this.HostMock.Setup(host => host.Dispose())
                .Callback(() => this.Events.Disposed = true);

            _ = this.HostMock.SetupAdd(host => host.Opened += It.IsAny<EventHandler>())
                .Callback<EventHandler>(handler => this.Events.AddOpened(handler));
            _ = this.HostMock.SetupRemove(host => host.Opened -= It.IsAny<EventHandler>())
                .Callback<EventHandler>(handler => this.Events.RemoveOpened(handler));

            _ = this.HostMock.SetupAdd(host => host.Closed += It.IsAny<EventHandler>())
                .Callback<EventHandler>(handler => this.Events.AddClosed(handler));
            _ = this.HostMock.SetupRemove(host => host.Closed -= It.IsAny<EventHandler>())
                .Callback<EventHandler>(handler => this.Events.RemoveClosed(handler));
        }

        public HostEvents Events { get; }

        public Mock<ICascadedMenuHost> HostMock { get; }

        public Mock<ICascadedMenuSurface> SurfaceMock { get; }

        public IRootMenuSurface? LastFactoryArgument { get; private set; }

        public ICascadedMenuHost InitializeHost(IRootMenuSurface? rootSurface)
        {
            this.LastFactoryArgument = rootSurface;
            this.HostMock.Object.RootSurface = rootSurface;
            return this.HostMock.Object;
        }
    }

    private sealed class HostEvents
    {
        private EventHandler? opened;
        private EventHandler? closed;

        public FrameworkElement? Anchor { get; set; }

        public Windows.Foundation.Point? LastPosition { get; set; }

        public MenuNavigationMode? LastNavigationMode { get; set; }

        public int ShowAtCalls { get; set; }

        public bool IsOpen { get; set; }

        public bool Disposed { get; set; }

        public int OpenedHandlerCount => this.opened?.GetInvocationList().Length ?? 0;

        public int ClosedHandlerCount => this.closed?.GetInvocationList().Length ?? 0;

        public void AddOpened(EventHandler handler) => this.opened += handler;

        public void RemoveOpened(EventHandler handler) => this.opened -= handler;

        public void AddClosed(EventHandler handler) => this.closed += handler;

        public void RemoveClosed(EventHandler handler) => this.closed -= handler;

        public void RaiseOpened(object sender) => this.opened?.Invoke(sender, EventArgs.Empty);

        public void RaiseClosed(object sender) => this.closed?.Invoke(sender, EventArgs.Empty);
    }
}
