// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Generic;
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

        harness.LastFactoryArgument.Should().NotBeNull();
        harness.LastFactoryArgument!.GetType().Name.Should().Contain("ContextMenuRootSurfaceAdapter");
        harness.HostMock.Object.RootSurface.Should().BeSameAs(harness.LastFactoryArgument);
        harness.Events.OpenedHandlerCount.Should().Be(1);
        harness.Events.ClosedHandlerCount.Should().Be(1);

        var storedHost = GetMenuHost(trigger);
        storedHost.Should().BeSameAs(harness.HostMock.Object);

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

        firstHarness.Events.Disposed.Should().BeTrue();
        firstHarness.Events.OpenedHandlerCount.Should().Be(0);
        firstHarness.Events.ClosedHandlerCount.Should().Be(0);

        secondHarness.LastFactoryArgument.Should().NotBeNull();
        secondHarness.Events.Disposed.Should().BeFalse();

        var storedHost = GetMenuHost(trigger);
        storedHost.Should().BeSameAs(secondHarness.HostMock.Object);

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

        harness.Events.Disposed.Should().BeTrue();
        harness.Events.OpenedHandlerCount.Should().Be(0);
        harness.Events.ClosedHandlerCount.Should().Be(0);
        GetMenuHost(trigger).Should().BeNull();

        InvokeContextRequested(trigger, CreateContextRequestedEventArgs());
        harness.Events.ShowAtCalls.Should().Be(0);

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

        harness.Events.Disposed.Should().BeTrue();
        GetMenuHost(trigger).Should().BeNull();
        harness.Events.OpenedHandlerCount.Should().Be(0);
        harness.Events.ClosedHandlerCount.Should().Be(0);

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
        initialAdapter.Should().NotBeNull();

        var args = CreateContextRequestedEventArgs();
        InvokeContextRequested(trigger, args);

        args.Handled.Should().BeTrue();
        harness.Events.ShowAtCalls.Should().Be(1);
        harness.Events.Anchor.Should().Be(trigger);
        harness.Events.LastNavigationMode.Should().Be(MenuNavigationMode.PointerInput);

        var appliedSource = harness.HostMock.Object.MenuSource.Should().BeOfType<MenuSourceView>().Which;
        appliedSource.Services.Should().BeSameAs(menuSource.Services);
        appliedSource.Items.Should().HaveCount(menuSource.Items.Count);

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
        secondView.Should().NotBeSameAs(firstView);
        secondView.Items.Should().ContainSingle(item => item.Text == "Secondary");

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

        harness.Events.IsOpen.Should().BeTrue();
        harness.HostMock.Object.RootSurface.Should().NotBeNull();
        harness.HostMock.Object.RootSurface!.Dismiss(MenuDismissKind.KeyboardInput);

        harness.Events.IsOpen.Should().BeFalse();

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
    {
        typeof(ContextMenu)
            .GetMethod("OnElementContextRequested", BindingFlags.NonPublic | BindingFlags.Static)!
            .Invoke(null, new object[] { element, args });
    }

    private static void InvokeElementUnloaded(UIElement element)
    {
        typeof(ContextMenu)
            .GetMethod("OnElementUnloaded", BindingFlags.NonPublic | BindingFlags.Static)!
            .Invoke(null, new object[] { element, new RoutedEventArgs() });
    }

    private sealed class HostHarness
    {
        public HostHarness()
        {
            this.Events = new HostEvents();
            this.SurfaceMock = new Mock<ICascadedMenuSurface>(MockBehavior.Loose);
            this.SurfaceMock.Setup(surface => surface.Dismiss(It.IsAny<MenuDismissKind>()));
            this.SurfaceMock.Setup(surface => surface.FocusFirstItem(It.IsAny<MenuLevel>(), It.IsAny<MenuNavigationMode>())).Returns(true);

            var rootElement = new Grid();

            this.HostMock = new Mock<ICascadedMenuHost>(MockBehavior.Strict);
            this.HostMock.SetupProperty(host => host.RootSurface);
            this.HostMock.SetupProperty(host => host.MenuSource);
            this.HostMock.SetupProperty(host => host.MaxLevelHeight);
            this.HostMock.SetupGet(host => host.Surface).Returns(this.SurfaceMock.Object);
            this.HostMock.SetupGet(host => host.RootElement).Returns(rootElement);
            this.HostMock.SetupGet(host => host.Anchor).Returns(() => this.Events.Anchor);
            this.HostMock.SetupGet(host => host.IsOpen).Returns(() => this.Events.IsOpen);

            this.HostMock.Setup(host => host.ShowAt(It.IsAny<FrameworkElement>(), It.IsAny<MenuNavigationMode>()))
                .Callback<FrameworkElement, MenuNavigationMode>((anchor, mode) =>
                {
                    this.Events.IsOpen = true;
                    this.Events.Anchor = anchor;
                    this.Events.LastNavigationMode = mode;
                })
                .Returns(true);

            this.HostMock.Setup(host => host.ShowAt(It.IsAny<FrameworkElement>(), It.IsAny<Windows.Foundation.Point>(), It.IsAny<MenuNavigationMode>()))
                .Callback<FrameworkElement, Windows.Foundation.Point, MenuNavigationMode>((anchor, point, mode) =>
                {
                    this.Events.IsOpen = true;
                    this.Events.Anchor = anchor;
                    this.Events.LastNavigationMode = mode;
                    this.Events.LastPosition = point;
                    this.Events.ShowAtCalls++;
                })
                .Returns(true);

            this.HostMock.Setup(host => host.Dismiss(It.IsAny<MenuDismissKind>()))
                .Callback(() => this.Events.IsOpen = false);

            this.HostMock.Setup(host => host.Dispose())
                .Callback(() => this.Events.Disposed = true);

            this.HostMock.SetupAdd(host => host.Opened += It.IsAny<EventHandler>())
                .Callback<EventHandler>(handler => this.Events.AddOpened(handler));
            this.HostMock.SetupRemove(host => host.Opened -= It.IsAny<EventHandler>())
                .Callback<EventHandler>(handler => this.Events.RemoveOpened(handler));

            this.HostMock.SetupAdd(host => host.Closed += It.IsAny<EventHandler>())
                .Callback<EventHandler>(handler => this.Events.AddClosed(handler));
            this.HostMock.SetupRemove(host => host.Closed -= It.IsAny<EventHandler>())
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

        public void AddOpened(EventHandler handler) => this.opened += handler;

        public void RemoveOpened(EventHandler handler) => this.opened -= handler;

        public void AddClosed(EventHandler handler) => this.closed += handler;

        public void RemoveClosed(EventHandler handler) => this.closed -= handler;

        public int OpenedHandlerCount => this.opened?.GetInvocationList().Length ?? 0;

        public int ClosedHandlerCount => this.closed?.GetInvocationList().Length ?? 0;

        public void RaiseOpened(object sender) => this.opened?.Invoke(sender, EventArgs.Empty);

        public void RaiseClosed(object sender) => this.closed?.Invoke(sender, EventArgs.Empty);
    }
}
