// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI.Xaml;
using Moq;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("MenuInteractionControllerTests")]
public sealed partial class MenuInteractionControllerTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task OnPointerEntered_WithOpenSubmenu_ShouldActivateRootAndRequestSubmenu() => EnqueueAsync(() =>
    {
        var harness = new ControllerHarness();
        var root = CreateMenuItem("File", hasChildren: true);
        var origin = new TestFrameworkElement();

        harness.ResetSurfaces();
        harness.Controller.OnPointerEntered(harness.RootContext, origin, root, menuOpen: true);

        _ = root.IsExpanded.Should().BeTrue();
        _ = harness.Controller.NavigationMode.Should().Be(MenuNavigationMode.PointerInput);

        harness.RootMock.Verify(m => m.FocusRoot(It.Is<MenuItemData>(mi => ReferenceEquals(mi, root)), MenuNavigationMode.PointerInput), Times.Exactly(2));

        harness.RootMock.Verify(m => m.OpenRootSubmenu(It.Is<MenuItemData>(mi => ReferenceEquals(mi, root)), origin, MenuNavigationMode.PointerInput), Times.Once());

        harness.ColumnMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 1)), Times.Exactly(2));
    });

    [TestMethod]
    public Task OnPointerEntered_WithClosedMenu_ShouldOnlyUpdateNavigationMode() => EnqueueAsync(() =>
    {
        var harness = new ControllerHarness();
        var root = CreateMenuItem("File", hasChildren: true);
        var origin = new TestFrameworkElement();

        harness.ResetSurfaces();
        harness.Controller.OnPointerEntered(harness.RootContext, origin, root, menuOpen: false);

        _ = harness.Controller.NavigationMode.Should().Be(MenuNavigationMode.PointerInput);
        _ = root.IsExpanded.Should().BeFalse();
        harness.RootMock.Verify(m => m.FocusRoot(It.IsAny<MenuItemData>(), It.IsAny<MenuNavigationMode>()), Times.Never());
        harness.RootMock.Verify(m => m.OpenRootSubmenu(It.IsAny<MenuItemData>(), It.IsAny<FrameworkElement>(), It.IsAny<MenuNavigationMode>()), Times.Never());
        harness.ColumnMock.Verify(m => m.CloseFromColumn(It.IsAny<int>()), Times.Never());
    });

    [TestMethod]
    public Task OnPointerEntered_ReenteringActiveRoot_ShouldNotDuplicateRequests() => EnqueueAsync(() =>
    {
        var harness = new ControllerHarness();
        var root = CreateMenuItem("Edit", hasChildren: true);
        var origin = new TestFrameworkElement();

        harness.Controller.OnFocusRequested(harness.RootContext, origin, root, MenuInteractionActivationSource.PointerInput, openSubmenu: true);
        harness.ResetSurfaces();

        harness.Controller.OnPointerEntered(harness.RootContext, origin, root, menuOpen: true);

        _ = root.IsExpanded.Should().BeTrue();
        _ = harness.Controller.NavigationMode.Should().Be(MenuNavigationMode.PointerInput);
        harness.RootMock.Verify(m => m.FocusRoot(It.IsAny<MenuItemData>(), It.IsAny<MenuNavigationMode>()), Times.Never());
        harness.RootMock.Verify(m => m.OpenRootSubmenu(It.IsAny<MenuItemData>(), It.IsAny<FrameworkElement>(), It.IsAny<MenuNavigationMode>()), Times.Never());
        harness.ColumnMock.Verify(m => m.CloseFromColumn(It.IsAny<int>()), Times.Never());
    });

    [TestMethod]
    public Task OnFocusRequested_WithKeyboard_ShouldOpenSubmenuAndSetNavigationMode() => EnqueueAsync(() =>
    {
        var harness = new ControllerHarness();
        var root = CreateMenuItem("Edit", hasChildren: true);
        var origin = new TestFrameworkElement();

        harness.ResetSurfaces();
        harness.Controller.OnFocusRequested(harness.RootContext, origin, root, MenuInteractionActivationSource.KeyboardInput, openSubmenu: true);

        _ = harness.Controller.NavigationMode.Should().Be(MenuNavigationMode.KeyboardInput);
        _ = root.IsExpanded.Should().BeTrue();

        harness.RootMock.Verify(m => m.OpenRootSubmenu(It.Is<MenuItemData>(mi => ReferenceEquals(mi, root)), origin, MenuNavigationMode.KeyboardInput), Times.Once());
        harness.ColumnMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 1)), Times.Exactly(2));
    });

    [TestMethod]
    public Task OnFocusRequested_WithKeyboardWithoutOpening_SubmenuShouldRemainClosed() => EnqueueAsync(() =>
    {
        var harness = new ControllerHarness();
        var root = CreateMenuItem("View", hasChildren: true);
        var origin = new TestFrameworkElement();

        harness.ResetSurfaces();
        harness.Controller.OnFocusRequested(harness.RootContext, origin, root, MenuInteractionActivationSource.KeyboardInput, openSubmenu: false);

        _ = harness.Controller.NavigationMode.Should().Be(MenuNavigationMode.KeyboardInput);
        _ = root.IsExpanded.Should().BeFalse();

        harness.RootMock.Verify(m => m.FocusRoot(It.Is<MenuItemData>(mi => ReferenceEquals(mi, root)), MenuNavigationMode.KeyboardInput), Times.Once());
        harness.RootMock.Verify(m => m.OpenRootSubmenu(It.IsAny<MenuItemData>(), It.IsAny<FrameworkElement>(), It.IsAny<MenuNavigationMode>()), Times.Never());
        harness.ColumnMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 1)), Times.Once());
    });

    [TestMethod]
    public Task OnSubmenuRequested_FromColumn_ShouldFocusItemAndOpenChildColumn() => EnqueueAsync(() =>
    {
        var harness = new ControllerHarness();
        var parent = CreateMenuItem("View", hasChildren: true);
        var origin = new TestFrameworkElement();

        harness.ResetSurfaces();
        harness.Controller.OnSubmenuRequested(harness.ColumnContext(1), origin, parent, MenuInteractionActivationSource.PointerInput);

        _ = parent.IsExpanded.Should().BeTrue();
        _ = harness.Controller.NavigationMode.Should().Be(MenuNavigationMode.PointerInput);

        harness.ColumnMock.Verify(m => m.FocusColumnItem(It.Is<MenuItemData>(mi => ReferenceEquals(mi, parent)), 1, MenuNavigationMode.PointerInput), Times.Once());
        harness.ColumnMock.Verify(m => m.OpenChildColumn(It.Is<MenuItemData>(mi => ReferenceEquals(mi, parent)), origin, 1, MenuNavigationMode.PointerInput), Times.Once());
    });

    [TestMethod]
    public Task OnSubmenuRequested_FromColumnWithKeyboard_ShouldActivateAndOpenChild() => EnqueueAsync(() =>
    {
        var harness = new ControllerHarness();
        var root = CreateMenuItem("Format", hasChildren: true);
        var parent = root.SubItems!.First();
        parent.SubItems = new[]
        {
            new MenuItemData
            {
                Id = "Format.Child.Grandchild",
                Text = "Format.Child.Grandchild",
            },
        };
        var origin = new TestFrameworkElement();

        var rootOrigin = new TestFrameworkElement();
        harness.Controller.OnFocusRequested(harness.RootContext, rootOrigin, root, MenuInteractionActivationSource.KeyboardInput, openSubmenu: true);
        var parentOrigin = new TestFrameworkElement();
        harness.Controller.OnFocusRequested(harness.ColumnContext(1), parentOrigin, parent, MenuInteractionActivationSource.KeyboardInput, openSubmenu: false);
        harness.ResetSurfaces();

        harness.Controller.OnSubmenuRequested(harness.ColumnContext(1), origin, parent, MenuInteractionActivationSource.KeyboardInput);

        harness.ColumnMock.Verify(m => m.FocusColumnItem(It.Is<MenuItemData>(mi => ReferenceEquals(mi, parent)), 1, MenuNavigationMode.KeyboardInput), Times.Once());
        harness.ColumnMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 2)), Times.Once());
        harness.ColumnMock.Verify(m => m.OpenChildColumn(It.Is<MenuItemData>(mi => ReferenceEquals(mi, parent)), origin, 1, MenuNavigationMode.KeyboardInput), Times.Once());
    });

    [TestMethod]
    public Task OnPointerEntered_ForLeafColumnItem_ShouldNotOpenChildColumn() => EnqueueAsync(() =>
    {
        var harness = new ControllerHarness();
        var root = CreateMenuItem("View", hasChildren: true);
        var leaf = root.SubItems!.First();
        var origin = new TestFrameworkElement();

        var rootOrigin = new TestFrameworkElement();
        harness.Controller.OnFocusRequested(harness.RootContext, rootOrigin, root, MenuInteractionActivationSource.PointerInput, openSubmenu: true);
        harness.ResetSurfaces();

        harness.Controller.OnPointerEntered(harness.ColumnContext(1), origin, leaf);

        _ = leaf.IsExpanded.Should().BeFalse();
        _ = harness.Controller.NavigationMode.Should().Be(MenuNavigationMode.PointerInput);

        harness.ColumnMock.Verify(m => m.FocusColumnItem(It.Is<MenuItemData>(mi => ReferenceEquals(mi, leaf)), 1, MenuNavigationMode.PointerInput), Times.Once());
        harness.ColumnMock.Verify(m => m.OpenChildColumn(It.IsAny<MenuItemData>(), It.IsAny<FrameworkElement>(), It.IsAny<int>(), It.IsAny<MenuNavigationMode>()), Times.Never());
        harness.ColumnMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 2)), Times.Once());
    });

    [TestMethod]
    public void OnInvokeRequested_FromColumn_ShouldInvokeItemAndDismissChain()
    {
        var harness = new ControllerHarness();
        var item = CreateMenuItem("Open");

        harness.ResetSurfaces();
        harness.Controller.OnInvokeRequested(harness.ColumnContext(1), item, MenuInteractionActivationSource.PointerInput);

        _ = harness.GroupSelections.Should().ContainSingle(x => ReferenceEquals(x, item));

        harness.ColumnMock.Verify(m => m.Invoke(It.Is<MenuItemData>(mi => ReferenceEquals(mi, item))), Times.Once());

        _ = item.IsExpanded.Should().BeFalse();
        _ = harness.Controller.NavigationMode.Should().Be(MenuNavigationMode.PointerInput);

        harness.ColumnMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 0)), Times.AtLeastOnce());
        harness.RootMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 0)), Times.AtLeastOnce());
        harness.RootMock.Verify(m => m.ReturnFocusToApp(), Times.Once());
    }

    [TestMethod]
    public void OnInvokeRequested_FromColumnWithKeyboard_ShouldRespectKeyboardState()
    {
        var harness = new ControllerHarness();
        var item = CreateMenuItem("Close");

        harness.ResetSurfaces();
        harness.Controller.OnInvokeRequested(harness.ColumnContext(1), item, MenuInteractionActivationSource.KeyboardInput);

        _ = harness.GroupSelections.Should().ContainSingle(x => ReferenceEquals(x, item));

        harness.ColumnMock.Verify(m => m.Invoke(It.Is<MenuItemData>(mi => ReferenceEquals(mi, item))), Times.Once());
        harness.ColumnMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 0)), Times.AtLeastOnce());
        harness.RootMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 0)), Times.AtLeastOnce());
        harness.RootMock.Verify(m => m.ReturnFocusToApp(), Times.Once());
        _ = harness.Controller.NavigationMode.Should().Be(MenuNavigationMode.KeyboardInput);
    }

    [TestMethod]
    public void OnInvokeRequested_FromRootWithKeyboard_ShouldInvokeAndDismiss()
    {
        var harness = new ControllerHarness();
        var item = CreateMenuItem("Preferences");

        harness.ResetSurfaces();
        harness.Controller.OnInvokeRequested(harness.RootContext, item, MenuInteractionActivationSource.KeyboardInput);

        _ = harness.GroupSelections.Should().ContainSingle(x => ReferenceEquals(x, item));

        harness.RootMock.Verify(m => m.Invoke(It.Is<MenuItemData>(mi => ReferenceEquals(mi, item))), Times.Once());
        harness.ColumnMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 0)), Times.AtLeastOnce());
        harness.RootMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 0)), Times.AtLeastOnce());
        harness.RootMock.Verify(m => m.ReturnFocusToApp(), Times.Once());
        _ = harness.Controller.NavigationMode.Should().Be(MenuNavigationMode.KeyboardInput);
    }

    [TestMethod]
    public Task RequestSubmenu_WithExplicitNavigationMode_ShouldActivateAndOpen() => EnqueueAsync(() =>
    {
        var harness = new ControllerHarness();
        var parent = CreateMenuItem("Help", hasChildren: true);
        var origin = new TestFrameworkElement();

        harness.Controller.OnSubmenuRequested(harness.RootContext, origin, parent, MenuInteractionActivationSource.KeyboardInput);

        _ = harness.Controller.NavigationMode.Should().Be(MenuNavigationMode.KeyboardInput);
        _ = parent.IsExpanded.Should().BeTrue();

        harness.RootMock.Verify(m => m.FocusRoot(It.Is<MenuItemData>(mi => ReferenceEquals(mi, parent)), MenuNavigationMode.KeyboardInput), Times.Once());
        harness.RootMock.Verify(m => m.OpenRootSubmenu(It.Is<MenuItemData>(mi => ReferenceEquals(mi, parent)), origin, MenuNavigationMode.KeyboardInput), Times.Once());
        harness.ColumnMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 1)), Times.Once());
    });

    [TestMethod]
    public Task Dismiss_ShouldClearStateAndReturnFocus() => EnqueueAsync(() =>
    {
        var harness = new ControllerHarness();
        var root = CreateMenuItem("File", hasChildren: true);
        var child = CreateMenuItem("File.New");

        var rootOrigin = new TestFrameworkElement();
        harness.Controller.OnFocusRequested(harness.RootContext, rootOrigin, root, MenuInteractionActivationSource.PointerInput, openSubmenu: true);
        var childOrigin = new TestFrameworkElement();
        harness.Controller.OnFocusRequested(harness.ColumnContext(1), childOrigin, child, MenuInteractionActivationSource.PointerInput, openSubmenu: false);

        harness.ResetSurfaces();
        harness.Controller.OnDismissRequested(harness.RootContext, MenuDismissKind.Programmatic);

        _ = root.IsExpanded.Should().BeFalse();
        _ = child.IsExpanded.Should().BeFalse();
        _ = harness.Controller.NavigationMode.Should().Be(MenuNavigationMode.PointerInput);
        harness.ColumnMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 0)), Times.AtLeastOnce());
        harness.RootMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 0)), Times.AtLeastOnce());
        harness.RootMock.Verify(m => m.ReturnFocusToApp(), Times.Once());
    });

    [TestMethod]
    public Task OnDismissRequested_ShouldClearActiveItemsAndReturnFocus() => EnqueueAsync(() =>
    {
        var harness = new ControllerHarness();
        var root = CreateMenuItem("File", hasChildren: true);
        var child = CreateMenuItem("File.New");

        var rootOrigin = new TestFrameworkElement();
        harness.Controller.OnFocusRequested(harness.RootContext, rootOrigin, root, MenuInteractionActivationSource.KeyboardInput, openSubmenu: true);
        var childOrigin = new TestFrameworkElement();
        harness.Controller.OnFocusRequested(harness.ColumnContext(1), childOrigin, child, MenuInteractionActivationSource.KeyboardInput, openSubmenu: false);

        harness.ResetSurfaces();
        harness.Controller.OnDismissRequested(harness.RootContext, MenuDismissKind.PointerInput);

        _ = root.IsExpanded.Should().BeFalse();
        _ = child.IsExpanded.Should().BeFalse();
        _ = harness.Controller.NavigationMode.Should().Be(MenuNavigationMode.PointerInput);
        harness.ColumnMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 0)), Times.AtLeastOnce());
        harness.RootMock.Verify(m => m.CloseFromColumn(It.Is<int>(i => i == 0)), Times.AtLeastOnce());
        harness.RootMock.Verify(m => m.ReturnFocusToApp(), Times.Once());
    });

    private static MenuItemData CreateMenuItem(string id, bool hasChildren = false)
    {
        var item = new MenuItemData
        {
            Id = id,
            Text = id,
        };

        if (hasChildren)
        {
            item.SubItems = new[]
            {
                new MenuItemData
                {
                    Id = $"{id}.Child",
                    Text = $"{id}.Child",
                },
            };
        }

        return item;
    }

    private static MenuServices CreateServices(Func<IReadOnlyDictionary<string, MenuItemData>> lookupAccessor, Action<MenuItemData> groupSelectionHandler)
    {
        var constructor = typeof(MenuServices).GetConstructor(
            BindingFlags.Instance | BindingFlags.NonPublic,
            binder: null,
            new[] { typeof(Func<IReadOnlyDictionary<string, MenuItemData>>), typeof(Action<MenuItemData>) },
            modifiers: null);

        return (MenuServices)constructor!.Invoke(new object[] { lookupAccessor, groupSelectionHandler });
    }

    private sealed class ControllerHarness
    {
        public ControllerHarness()
        {
            this.Lookup = new Dictionary<string, MenuItemData>(StringComparer.OrdinalIgnoreCase);
            this.GroupSelections = new List<MenuItemData>();
            var services = CreateServices(() => this.Lookup, item => this.GroupSelections.Add(item));
            this.Controller = new MenuInteractionController(services);

            this.RootMock = new Mock<IMenuInteractionSurface>(MockBehavior.Loose);
            this.ColumnMock = new Mock<IMenuInteractionSurface>(MockBehavior.Loose);
        }

        public MenuInteractionController Controller { get; }

        public Mock<IMenuInteractionSurface> RootMock { get; }

        public Mock<IMenuInteractionSurface> ColumnMock { get; }

        public Dictionary<string, MenuItemData> Lookup { get; }

        public List<MenuItemData> GroupSelections { get; }

        public MenuInteractionContext RootContext => MenuInteractionContext.ForRoot(this.RootMock.Object, this.ColumnMock.Object);

        public MenuInteractionContext ColumnContext(int columnLevel) => MenuInteractionContext.ForColumn(columnLevel, this.ColumnMock.Object, this.RootMock.Object);

        public void ResetSurfaces()
        {
            this.RootMock.Reset();
            this.ColumnMock.Reset();
        }
    }

    private sealed partial class TestFrameworkElement : FrameworkElement
    {
    }
}
