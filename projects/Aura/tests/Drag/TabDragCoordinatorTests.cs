// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Windowing;
using DroidNet.Coordinates;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Moq;
using Windows.Foundation;
using WinRT.Interop;

namespace DroidNet.Aura.Drag.Tests;

/// <summary>
/// UI tests for <see cref="TabDragCoordinator"/> verifying drag lifecycle orchestration
/// and interactions with window/spatial services using mock dependencies.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("TabDragCoordinatorTests")]
[TestCategory("UITest")]
public class TabDragCoordinatorTests : VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public Task StartDrag_WithValidParameters_InitializesContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var draggedIndex = 1;
        var tabStripMock = new TabStripMockBuilder()
            .WithDraggedItemSnapshot(draggedIndex)
            .Build();
        var draggedItem = "Item1";
        var visualElement = await this.CreateLoadedVisualElementAsync().ConfigureAwait(true);

        var factoryInvocations = new List<(Window? window, FrameworkElement? element)>();
        var coordinator = this.CreateCoordinator(
            mapperFactory: (window, element) =>
            {
                factoryInvocations.Add((window, element));
                var mapperMock = new Mock<ISpatialMapper>();
                mapperMock.Setup(m => m.Convert<ScreenSpace, PhysicalScreenSpace>(It.IsAny<SpatialPoint<ScreenSpace>>()))
                    .Returns<SpatialPoint<ScreenSpace>>(sp => new SpatialPoint<PhysicalScreenSpace>(sp.Point));
                return mapperMock.Object;
            });

        var initialPoint = CreateScreenPoint(120, 30);

        // Act
        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, initialPoint);

        // Assert
        factoryInvocations.Should().HaveCount(1);
        factoryInvocations[0].window.Should().Be(VisualUserInterfaceTestsApp.MainWindow);
        factoryInvocations[0].element.Should().Be(visualElement, "factory should be called with the visual element, not the TabStrip interface");

        var isActive = GetPrivateField<bool>(coordinator, "isActive");
        isActive.Should().BeTrue();

        var context = GetPrivateField<DragContext?>(coordinator, "dragContext");
        context.Should().NotBeNull();
        context!.TabStrip.Should().Be(tabStripMock.Object);
        context.DraggedItem.Should().Be(draggedItem);
        context.DraggedItemIndex.Should().Be(draggedIndex);

        var lastPhysical = GetPrivateField<SpatialPoint<PhysicalScreenSpace>>(coordinator, "lastCursorPosition");
        AreClose(lastPhysical.Point, initialPoint.Point).Should().BeTrue();

        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_ThrowsWhenAlreadyActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var tabStripMock = new TabStripMockBuilder()
            .WithDraggedItemSnapshot(0)
            .Build();
        var visualElement = await this.CreateLoadedVisualElementAsync().ConfigureAwait(true);
        var initialPoint = CreateScreenPoint(80, 20);

        coordinator.StartDrag("FirstItem", 0, tabStripMock.Object, visualElement, initialPoint);

        // Act
        var act = () => coordinator.StartDrag("SecondItem", 0, tabStripMock.Object, visualElement, initialPoint);

        // Assert
        act.Should().Throw<InvalidOperationException>().WithMessage("*already active*");

        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_ThrowsWhenItemNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var tabStrip = Mock.Of<ITabStrip>();
        var visualElement = await this.CreateLoadedVisualElementAsync().ConfigureAwait(true);

        // Act
        var act = () => coordinator.StartDrag(null!, 0, tabStrip, visualElement, CreateScreenPoint(60, 18));

        // Assert
        act.Should().Throw<ArgumentNullException>().WithParameterName("item");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_ThrowsWhenSourceNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var visualElement = await this.CreateLoadedVisualElementAsync().ConfigureAwait(true);

        // Act
        var act = () => coordinator.StartDrag("Item", 0, null!, visualElement, CreateScreenPoint(40, 22));

        // Assert
        act.Should().Throw<ArgumentNullException>().WithParameterName("source");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_ThrowsWhenVisualElementNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var tabStrip = Mock.Of<ITabStrip>();

        // Act
        var act = () => coordinator.StartDrag("Item", 0, tabStrip, null!, CreateScreenPoint(55, 35));

        // Assert
        act.Should().Throw<ArgumentNullException>().WithParameterName("visualElement");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDrag_WhenActive_CompletesStrategyAndResetsState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var draggedIndex = 1;
        var coordinator = this.CreateCoordinator();
        var tabStripMock = new TabStripMockBuilder()
            .WithDraggedItemSnapshot(draggedIndex)
            .Build();
        var visualElement = await this.CreateLoadedVisualElementAsync().ConfigureAwait(true);
        var initialPoint = CreateScreenPoint(120, 30);

        coordinator.StartDrag("Item", draggedIndex, tabStripMock.Object, visualElement, initialPoint);

        var strategy = new StubDragStrategy();
        SetPrivateField(coordinator, "currentStrategy", strategy);

        // Act
        coordinator.EndDrag(CreateScreenPoint(140, 40));

        // Assert
        strategy.CompleteCalled.Should().BeTrue();
        strategy.ReturnedIndex.Should().BeNull("stub strategy returns null");
        GetPrivateField<bool>(coordinator, "isActive").Should().BeFalse();
        GetPrivateField<DragContext?>(coordinator, "dragContext").Should().BeNull();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDrag_WhenInactive_DoesNotInvokeStrategy_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var strategy = new StubDragStrategy();
        SetPrivateField(coordinator, "currentStrategy", strategy);

        // Act
        coordinator.EndDrag(CreateScreenPoint(20, 15));

        // Assert
        strategy.CompleteCalled.Should().BeFalse();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Abort_WhenActive_EndsStrategyAndClearsState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var draggedIndex = 1;
        var coordinator = this.CreateCoordinator();
        var tabStripMock = new TabStripMockBuilder()
            .WithDraggedItemSnapshot(draggedIndex)
            .Build();
        var visualElement = await this.CreateLoadedVisualElementAsync().ConfigureAwait(true);

        coordinator.StartDrag("Item", draggedIndex, tabStripMock.Object, visualElement, CreateScreenPoint(100, 25));

        var strategy = new StubDragStrategy();
        SetPrivateField(coordinator, "currentStrategy", strategy);

        // Act
        coordinator.Abort();

        // Assert
        strategy.CompleteCalled.Should().BeTrue();
        strategy.ReturnedIndex.Should().BeNull("abort returns null");
        GetPrivateField<bool>(coordinator, "isActive").Should().BeFalse();
        GetPrivateField<DragContext?>(coordinator, "dragContext").Should().BeNull();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Abort_WhenInactive_IsIgnored_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var strategy = new StubDragStrategy();
        SetPrivateField(coordinator, "currentStrategy", strategy);

        // Act
        coordinator.Abort();

        // Assert
        strategy.CompleteCalled.Should().BeFalse();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task RegisterTabStrip_AddsReference_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var tabStripMock = new TabStripMockBuilder().Build();

        // Act
        coordinator.RegisterTabStrip(tabStripMock.Object);

        // Assert
        var registered = GetRegisteredStrips(coordinator);
        registered.Should().ContainSingle();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task UnregisterTabStrip_RemovesReference_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var stripAMock = new TabStripMockBuilder().Build();
        var stripBMock = new TabStripMockBuilder().Build();

        coordinator.RegisterTabStrip(stripAMock.Object);
        coordinator.RegisterTabStrip(stripBMock.Object);

        // Act
        coordinator.UnregisterTabStrip(stripAMock.Object);

        // Assert
        var registered = GetRegisteredStrips(coordinator);
        registered.Should().ContainSingle().Which.Should().Be(stripBMock.Object);

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDrag_InTearOutMode_WithoutHitStrip_RaisesTabTearOutRequested_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await this.CreateLoadedVisualElementAsync().ConfigureAwait(true);
        var draggedIndex = 0;
        var tabStripMock = new TabStripMockBuilder()
            .WithDraggedItemSnapshot(draggedIndex)
            .Build();
        var draggedItem = "ItemA";
        var initialPoint = CreateScreenPoint(120, 30);

        // Setup drag visual service mock for TearOut mode
        var dragServiceMock = new DragVisualServiceMockBuilder()
            .WithSessionToken(new DragSessionToken())
            .WithDescriptor(new DragVisualDescriptor())
            .Build();

        var coordinator = this.CreateCoordinator(dragService: dragServiceMock.Object);
        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, initialPoint);

        // Switch to TearOut mode explicitly via SwitchToTearOutMode
        var switchMethod = typeof(TabDragCoordinator).GetMethod(
            "SwitchToTearOutMode",
            BindingFlags.Instance | BindingFlags.NonPublic);
        switchMethod?.Invoke(coordinator, [initialPoint]);

        // Act: Drop outside any TabStrip (far away point that won't hit test)
        var dropPoint = CreateScreenPoint(5000, 5000);
        coordinator.EndDrag(dropPoint);

        // Assert
        tabStripMock.Verify(s => s.TearOutTab(draggedItem, It.IsAny<SpatialPoint<ScreenSpace>>()), Times.Once());
        tabStripMock.Verify(s => s.CompleteDrag(draggedItem, null, null), Times.Once());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDrag_WithHitStrip_CallsCompleteDragWithDestination_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await this.CreateLoadedVisualElementAsync().ConfigureAwait(true);
        var draggedIndex = 0;
        var tabStripMock = new TabStripMockBuilder()
            .WithDraggedItemSnapshot(draggedIndex)
            .Build();
        var draggedItem = "ItemA";
        var initialPoint = CreateScreenPoint(120, 30);

        var coordinator = this.CreateCoordinator();
        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, initialPoint);

        // Register the strip so it can be hit-tested
        coordinator.RegisterTabStrip(tabStripMock.Object);

        // Use ReorderStrategy which returns an actual drop index
        var reorderStrategy = new StubDragStrategy { ReturnIndexOverride = 2 };
        SetPrivateField(coordinator, "currentStrategy", reorderStrategy);

        // Act: Drop within the TabStrip bounds
        var dropPoint = CreateScreenPoint(50, 20);
        coordinator.EndDrag(dropPoint);

        // Assert
        reorderStrategy.CompleteCalled.Should().BeTrue();
        tabStripMock.Verify(s => s.CompleteDrag(draggedItem, tabStripMock.Object, 2), Times.Once());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task SwitchToTearOutMode_CallsCloseTabOnSource_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await this.CreateLoadedVisualElementAsync().ConfigureAwait(true);
        var draggedIndex = 0;
        var tabStripMock = new TabStripMockBuilder()
            .WithDraggedItemSnapshot(draggedIndex)
            .Build();
        var draggedItem = "ItemA";
        var initialPoint = CreateScreenPoint(120, 30);

        // Setup drag visual service mock for TearOut mode
        var dragServiceMock = new DragVisualServiceMockBuilder()
            .WithSessionToken(new DragSessionToken())
            .WithDescriptor(new DragVisualDescriptor())
            .Build();

        var coordinator = this.CreateCoordinator(dragService: dragServiceMock.Object);
        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, initialPoint);

        // Act: Switch to TearOut mode by calling the private method via reflection
        var method = typeof(TabDragCoordinator).GetMethod(
            "SwitchToTearOutMode",
            BindingFlags.Instance | BindingFlags.NonPublic);
        method?.Invoke(coordinator, [initialPoint]);

        // Assert
        tabStripMock.Verify(s => s.CloseTab(draggedItem), Times.Once());

        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task RegisterTabStrip_AddsMultipleReferences_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var secondStripMock = new TabStripMockBuilder().Build();
        var thirdStripMock = new TabStripMockBuilder().Build();

        // Act
        coordinator.RegisterTabStrip(secondStripMock.Object);
        coordinator.RegisterTabStrip(thirdStripMock.Object);

        // Assert
        var registered = GetRegisteredStrips(coordinator);
        registered.Should().Contain(secondStripMock.Object, "second strip should be registered");
        registered.Should().Contain(thirdStripMock.Object, "third strip should be registered");
        registered.Count.Should().Be(2, "exactly two strips should be registered");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Abort_WhenActive_CompletesStrategyAndCleansUpState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await this.CreateLoadedVisualElementAsync().ConfigureAwait(true);
        var draggedIndex = 0;
        var tabStripMock = new TabStripMockBuilder()
            .WithDraggedItemSnapshot(draggedIndex)
            .Build();

        var coordinator = this.CreateCoordinator();
        coordinator.StartDrag("Item", draggedIndex, tabStripMock.Object, visualElement, CreateScreenPoint(100, 25));

        var strategy = new StubDragStrategy();
        SetPrivateField(coordinator, "currentStrategy", strategy);

        // Act
        coordinator.Abort();

        // Assert
        strategy.CompleteCalled.Should().BeTrue("strategy should be completed during abort");
        strategy.ReturnedIndex.Should().BeNull("abort returns null");
        GetPrivateField<bool>(coordinator, "isActive").Should().BeFalse();
        GetPrivateField<DragContext?>(coordinator, "dragContext").Should().BeNull();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_WithNullInitialPoint_UsesGetCursorPos_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await this.CreateLoadedVisualElementAsync().ConfigureAwait(true);
        var tabStripMock = new TabStripMockBuilder()
            .WithDraggedItemSnapshot(0)
            .Build();

        var coordinator = this.CreateCoordinator();

        // Act: Start drag without providing initial point (null)
        coordinator.StartDrag("Item", 0, tabStripMock.Object, visualElement, initialScreenPoint: null);

        // Assert: Should successfully initialize (GetCursorPos is called internally)
        var isActive = GetPrivateField<bool>(coordinator, "isActive");
        isActive.Should().BeTrue("drag should be active even without explicit initial point");

        var context = GetPrivateField<DragContext?>(coordinator, "dragContext");
        context.Should().NotBeNull("context should be initialized");

        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDrag_InReorderMode_WhenStrategyReturnsNull_CallsCompleteDragWithNullIndex_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await this.CreateLoadedVisualElementAsync().ConfigureAwait(true);
        var draggedIndex = 0;
        var tabStripMock = new TabStripMockBuilder()
            .WithDraggedItemSnapshot(draggedIndex)
            .Build();
        var draggedItem = "ItemA";
        var initialPoint = CreateScreenPoint(120, 30);

        var coordinator = this.CreateCoordinator();
        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, initialPoint);
        coordinator.RegisterTabStrip(tabStripMock.Object);

        // Use a strategy that returns null (error case)
        var nullReturningStrategy = new StubDragStrategy { ReturnIndexOverride = null };
        SetPrivateField(coordinator, "currentStrategy", nullReturningStrategy);

        // Act: Drop within bounds but strategy returns null (error case)
        var dropPoint = CreateScreenPoint(50, 20);
        coordinator.EndDrag(dropPoint);

        // Assert: hitStrip is not null but finalDropIndex is null => error case
        nullReturningStrategy.CompleteCalled.Should().BeTrue();
        tabStripMock.Verify(s => s.CompleteDrag(draggedItem, null, null), Times.Once());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    #region Helper Methods

    private TabDragCoordinator CreateCoordinator(
        IWindowManagerService? windowManager = null,
        SpatialMapperFactory? mapperFactory = null,
        IDragVisualService? dragService = null)
    {
        if (windowManager == null)
        {
            var wmMock = new Mock<IWindowManagerService>();
            var windowContext = new WindowContext
            {
                Id = Win32Interop.GetWindowIdFromWindow(WindowNative.GetWindowHandle(VisualUserInterfaceTestsApp.MainWindow)),
                Window = VisualUserInterfaceTestsApp.MainWindow,
                Category = WindowCategory.System,
                CreatedAt = DateTimeOffset.UtcNow,
            };

            wmMock.Setup(wm => wm.GetWindow(It.IsAny<WindowId>())).Returns(windowContext);
            wmMock.Setup(wm => wm.OpenWindows).Returns(new[] { windowContext });
            windowManager = wmMock.Object;
        }

        return new TabDragCoordinator(
            windowManager,
            mapperFactory ?? ((w, e) => Mock.Of<ISpatialMapper>()),
            dragService ?? Mock.Of<IDragVisualService>(),
            this.LoggerFactory);
    }

    private async Task<Border> CreateLoadedVisualElementAsync()
    {
        var element = new Border
        {
            Width = 100,
            Height = 40,
            Background = new SolidColorBrush(Colors.LightGray),
        };
        await LoadTestContentAsync(element).ConfigureAwait(true);
        await DragTestHelpers.WaitForRenderAsync().ConfigureAwait(true);
        return element;
    }

    private static SpatialPoint<ScreenSpace> CreateScreenPoint(double x, double y) => DragTestHelpers.ScreenPoint(x, y);

    private static bool AreClose(Point left, Point right, double tolerance = 0.1) => DragTestHelpers.AreClose(left, right, tolerance);

    private static TField GetPrivateField<TField>(object instance, string fieldName)
    {
        var field = instance.GetType().GetField(fieldName, BindingFlags.Instance | BindingFlags.NonPublic);
        return field is null ? default! : (TField)field.GetValue(instance)!;
    }

    private static void SetPrivateField(object instance, string fieldName, object? value)
    {
        var field = instance.GetType().GetField(fieldName, BindingFlags.Instance | BindingFlags.NonPublic);
        field?.SetValue(instance, value);
    }

    private static List<ITabStrip> GetRegisteredStrips(TabDragCoordinator coordinator)
    {
        var field = coordinator.GetType().GetField("registeredStrips", BindingFlags.Instance | BindingFlags.NonPublic);
        if (field?.GetValue(coordinator) is not List<WeakReference<ITabStrip>> references)
        {
            return [];
        }

        return references
            .Select(reference => reference.TryGetTarget(out var strip) ? strip : null)
            .Where(strip => strip is not null)
            .Cast<ITabStrip>()
            .ToList();
    }

    private static async Task WaitForRenderCompletionAsync() =>
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

    #endregion

    #region Test Helpers

    private sealed class StubDragStrategy : IDragStrategy
    {
        public bool CompleteCalled { get; private set; }

        public int? ReturnedIndex { get; private set; }

        public int? ReturnIndexOverride { get; set; }

        public bool? LastDropArgument { get; private set; }

        public DragContext? LastContext { get; private set; }

        public SpatialPoint<PhysicalScreenSpace>? LastInitiatePosition { get; private set; }

        public SpatialPoint<PhysicalScreenSpace>? LastMovePosition { get; private set; }

        public int? CompleteDrag(bool drop)
        {
            this.CompleteCalled = true;
            this.LastDropArgument = drop;
            this.ReturnedIndex = this.ReturnIndexOverride;
            return this.ReturnedIndex;
        }

        public void InitiateDrag(DragContext context, SpatialPoint<PhysicalScreenSpace> position)
        {
            this.LastContext = context;
            this.LastInitiatePosition = position;
        }

        public void OnDragPositionChanged(SpatialPoint<PhysicalScreenSpace> position)
        {
            this.LastMovePosition = position;
        }
    }

    #endregion
}
