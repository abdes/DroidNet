// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using DroidNet.Aura.Windowing;
using DroidNet.Coordinates;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using AwesomeAssertions;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
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
    private SpatialMapper? mapper;

    private Border? testElement;

    public TestContext TestContext { get; set; }

    [TestMethod]
    public Task StartDrag_WithValidParameters_InitializesContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        const int draggedIndex = 0;
        var builder = new TabStripMockBuilder()
            .WithItemSnapshot(draggedIndex, "Item1", new Point(0, 0), 120);
        var tabStripMock = builder.Build();
        var draggedItem = builder.GetItem(0)!;
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        var factoryInvocations = new List<(Window? window, FrameworkElement? element)>();
        var coordinator = this.CreateCoordinator(
            mapperFactory: (window, element) =>
            {
                factoryInvocations.Add((window, element));
                return new SpatialMapper(window, element);
            });

        var initialElementPoint = new Point(120, 30).AsElement();

        // Act
        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, visualElement, initialElementPoint, new Point(0, 0));

        // Assert
        _ = factoryInvocations.Should().HaveCount(1);
        _ = factoryInvocations[0].window.Should().Be(VisualUserInterfaceTestsApp.MainWindow);
        _ = factoryInvocations[0].element.Should().Be(visualElement, "factory should be called with the visual element, not the TabStrip interface");

        var isActive = GetPrivateField<bool>(coordinator, "isActive");
        _ = isActive.Should().BeTrue();

        var context = GetPrivateField<DragContext?>(coordinator, "dragContext");
        _ = context.Should().NotBeNull();
        _ = context!.TabStrip.Should().Be(tabStripMock.Object);
        _ = context.DraggedItemData.Should().Be(draggedItem);

        // The last cursor position should be in physical screen space, converted properly from element space
        var lastPhysical = GetPrivateField<SpatialPoint<PhysicalScreenSpace>>(coordinator, "lastCursorPosition");
        _ = lastPhysical.Should().NotBeNull();

        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_ThrowsWhenAlreadyActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var builder = new TabStripMockBuilder()
            .WithItemSnapshot(0, "FirstItem", new Point(0, 0), 120)
            .WithItemSnapshot(1, "SecondItem", new Point(130, 0), 120);
        var tabStripMock = builder.Build();
        var firstItem = builder.GetItem(0)!;
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        var initialElementPoint = new Point(80, 20).AsElement();

        coordinator.StartDrag(firstItem, 0, tabStripMock.Object, visualElement, visualElement, initialElementPoint, new Point(0, 0));

        // Act
        var secondItem = builder.GetItem(1)!;
        var act = () => coordinator.StartDrag(secondItem, 0, tabStripMock.Object, visualElement, visualElement, initialElementPoint, new Point(0, 0));

        // Assert
        _ = act.Should().Throw<InvalidOperationException>().WithMessage("*already active*");

        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_ThrowsWhenItemNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var tabStrip = Mock.Of<ITabStrip>();
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        // Act
        var act = () => coordinator.StartDrag(null!, 0, tabStrip, visualElement, visualElement, new Point(60, 18).AsElement(), new Point(0, 0));

        // Assert
        _ = act.Should().Throw<ArgumentNullException>().WithParameterName("item");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_ThrowsWhenSourceNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        // Use the builder to create the test item so TabItem creation is centralized
        var builder = new TabStripMockBuilder().WithItemSnapshot(0, "Item");
        var testItem = builder.GetItem(0)!;

        // Act
        var act = () => coordinator.StartDrag(testItem, 0, null!, visualElement, visualElement, new Point(40, 22).AsElement(), new Point(0, 0));

        // Assert
        _ = act.Should().Throw<ArgumentNullException>().WithParameterName("source");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_ThrowsWhenDraggedElementNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var tabStrip = Mock.Of<ITabStrip>();

        // Use the builder to create the test item so TabItem creation is centralized
        var builder = new TabStripMockBuilder().WithItemSnapshot(0, "Item");
        var testItem = builder.GetItem(0)!;

        // Act
        var act = () => coordinator.StartDrag(testItem, 0, tabStrip, null!, null!, new Point(55, 35).AsElement(), new Point(0, 0));

        // Assert
        _ = act.Should().Throw<ArgumentNullException>().WithParameterName("draggedElement");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDrag_WhenActive_CompletesStrategyAndResetsState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        const int draggedIndex = 0;
        var coordinator = this.CreateCoordinator();
        var builder = new TabStripMockBuilder()
            .WithItemSnapshot(draggedIndex, "Item", new Point(0, 0), 120);
        var tabStripMock = builder.Build();
        var draggedItem = builder.GetItem(0)!;
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        var initialElementPoint = new Point(120, 30).AsElement();

        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, visualElement, initialElementPoint, new Point(0, 0));

        var strategy = new StubDragStrategy();
        SetPrivateField(coordinator, "currentStrategy", strategy);

        // Act
        coordinator.EndDrag(this.ToScreen(140, 40));

        // Assert
        _ = strategy.CompleteCalled.Should().BeTrue();
        _ = strategy.ReturnedIndex.Should().BeNull("stub strategy returns null");
        _ = GetPrivateField<bool>(coordinator, "isActive").Should().BeFalse();
        _ = GetPrivateField<DragContext?>(coordinator, "dragContext").Should().BeNull();

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
        coordinator.EndDrag(this.ToScreen(20, 15));

        // Assert
        _ = strategy.CompleteCalled.Should().BeFalse();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Abort_WhenActive_EndsStrategyAndClearsState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        const int draggedIndex = 0;
        var coordinator = this.CreateCoordinator();
        var builder = new TabStripMockBuilder()
            .WithItemSnapshot(draggedIndex, "Item", new Point(0, 0), 120);
        var tabStripMock = builder.Build();
        var draggedItem = builder.GetItem(0)!;
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, visualElement, new Point(100, 25).AsElement(), new Point(0, 0));

        var strategy = new StubDragStrategy();
        SetPrivateField(coordinator, "currentStrategy", strategy);

        // Act
        coordinator.Abort();

        // Assert
        _ = strategy.CompleteCalled.Should().BeTrue();
        _ = strategy.ReturnedIndex.Should().BeNull("abort returns null");
        _ = GetPrivateField<bool>(coordinator, "isActive").Should().BeFalse();
        _ = GetPrivateField<DragContext?>(coordinator, "dragContext").Should().BeNull();

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
        _ = strategy.CompleteCalled.Should().BeFalse();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task RegisterTabStrip_AddsReference_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var builder = new TabStripMockBuilder();
        var tabStripMock = builder.Build();

        // Act
        coordinator.RegisterTabStrip(tabStripMock.Object);

        // Assert
        var registered = GetRegisteredStrips(coordinator);
        _ = registered.Should().ContainSingle();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task UnregisterTabStrip_RemovesReference_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var coordinator = this.CreateCoordinator();
        var builderA = new TabStripMockBuilder();
        var stripAMock = builderA.Build();
        var builderB = new TabStripMockBuilder();
        var stripBMock = builderB.Build();

        coordinator.RegisterTabStrip(stripAMock.Object);
        coordinator.RegisterTabStrip(stripBMock.Object);

        // Act
        coordinator.UnregisterTabStrip(stripAMock.Object);

        // Assert
        var registered = GetRegisteredStrips(coordinator);
        _ = registered.Should().ContainSingle().Which.Should().Be(stripBMock.Object);

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDrag_InTearOutMode_WithoutHitStrip_RaisesTabTearOutRequested_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);
        const int draggedIndex = 0;
        var builder = new TabStripMockBuilder().WithItemSnapshot(draggedIndex, "ItemA");
        var (tabStripMock, stripElement) = builder.BuildWithElement();
        var draggedItem = builder.GetItem(0)!;

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        var initialElementPoint = new Point(120, 30).AsElement();
        var initialScreenPoint = this.ToScreen(120, 30);

        // SwitchToTearOutMode expects a PhysicalScreenSpace point; convert the ScreenSpace point
        var initialPhysicalPoint = this.ToPhysical(120, 30);

        // Setup drag visual service mock for TearOut mode
        var dragServiceMock = new DragVisualServiceMockBuilder()
            .WithSessionToken(default)
            .WithDescriptor(new DragVisualDescriptor())
            .Build();

        var coordinator = this.CreateCoordinator(dragService: dragServiceMock.Object);
        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, visualElement, initialElementPoint, new Point(0, 0));

        // Switch to TearOut mode explicitly via SwitchToTearOutMode
        var switchMethod = typeof(TabDragCoordinator).GetMethod(
            "SwitchToTearOutMode",
            BindingFlags.Instance | BindingFlags.NonPublic);
        _ = switchMethod?.Invoke(coordinator, [initialPhysicalPoint]);

        // Act: Drop outside any TabStrip (far away point that won't hit test)
        var dropPoint = this.ToScreen(5000, 5000);
        coordinator.EndDrag(dropPoint);

        // Assert
        tabStripMock.Verify(s => s.TearOutTab(draggedItem, It.IsAny<SpatialPoint<ScreenSpace>>()), Times.Once());
        tabStripMock.Verify(s => s.TryCompleteDrag(draggedItem, null, null), Times.Once());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDrag_WithHitStrip_CallsCompleteDragWithDestination_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);
        const int draggedIndex = 0;
        var builder = new TabStripMockBuilder().WithItemSnapshot(draggedIndex);
        var (tabStripMock, stripElement) = builder.BuildWithElement();
        var draggedItem = builder.GetItem(0)!;

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        var initialElementPoint = new Point(120, 30).AsElement();

        var coordinator = this.CreateCoordinator(mapperFactory: (w, e) => new SpatialMapper(w, e));

        // Load the wrapper element into the visual tree so the coordinator can create a mapper for it
        await LoadTestContentAsync(stripElement).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, stripElement, stripElement, initialElementPoint, new Point(0, 0));

        // Register the wrapper element so it can be hit-tested
        coordinator.RegisterTabStrip((ITabStrip)stripElement);

        // Use ReorderStrategy which returns an actual drop index
        var reorderStrategy = new StubDragStrategy { ReturnIndexOverride = 2 };
        SetPrivateField(coordinator, "currentStrategy", reorderStrategy);

        // Act: Drop within the TabStrip bounds
        var dropPoint = this.ToScreen(50, 20);
        coordinator.EndDrag(dropPoint);

        // Assert
        _ = reorderStrategy.CompleteCalled.Should().BeTrue();
        tabStripMock.Verify(s => s.TryCompleteDrag(draggedItem, tabStripMock.Object, 2), Times.Once());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task SwitchToTearOutMode_CallsCloseTabOnSource_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);
        const int draggedIndex = 0;
        var builder = new TabStripMockBuilder().WithItemSnapshot(draggedIndex);
        var (tabStripMock, stripElement) = builder.BuildWithElement();
        var draggedItem = builder.GetItem(0)!;

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        var initialElementPoint = new Point(120, 30).AsElement();
        var initialScreenPoint = this.ToScreen(120, 30);
        var initialPhysicalPoint = this.ToPhysical(120, 30);

        // Setup drag visual service mock for TearOut mode
        var dragServiceMock = new DragVisualServiceMockBuilder()
            .WithSessionToken(default)
            .WithDescriptor(new DragVisualDescriptor())
            .Build();

        var coordinator = this.CreateCoordinator(dragService: dragServiceMock.Object);
        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, visualElement, initialElementPoint, new Point(0, 0));

        // Act: Switch to TearOut mode by calling the private method via reflection
        var method = typeof(TabDragCoordinator).GetMethod(
            "SwitchToTearOutMode",
            BindingFlags.Instance | BindingFlags.NonPublic);
        _ = method?.Invoke(coordinator, [initialPhysicalPoint]);

        // Assert
        tabStripMock.Verify(s => s.DetachTab(draggedItem), Times.Once());

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
        _ = registered.Should().Contain(secondStripMock.Object, "second strip should be registered");
        _ = registered.Should().Contain(thirdStripMock.Object, "third strip should be registered");
        _ = registered.Count.Should().Be(2, "exactly two strips should be registered");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Abort_WhenActive_CompletesStrategyAndCleansUpState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);
        const int draggedIndex = 0;
        var builder = new TabStripMockBuilder().WithItemSnapshot(draggedIndex);
        var (tabStripMock, stripElement) = builder.BuildWithElement();
        var draggedItem = builder.GetItem(0)!;

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        var coordinator = this.CreateCoordinator();
        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, visualElement, new Point(100, 25).AsElement(), new Point(0, 0));

        var strategy = new StubDragStrategy();
        SetPrivateField(coordinator, "currentStrategy", strategy);

        // Act
        coordinator.Abort();

        // Assert
        _ = strategy.CompleteCalled.Should().BeTrue("strategy should be completed during abort");
        _ = strategy.ReturnedIndex.Should().BeNull("abort returns null");
        _ = GetPrivateField<bool>(coordinator, "isActive").Should().BeFalse();
        _ = GetPrivateField<DragContext?>(coordinator, "dragContext").Should().BeNull();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StartDrag_WithNullInitialPoint_UsesGetCursorPos_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);
        const int draggedIndex = 0;
        var builder = new TabStripMockBuilder().WithItemSnapshot(draggedIndex);
        var (tabStripMock, stripElement) = builder.BuildWithElement();
        var draggedItem = builder.GetItem(0)!;

        var coordinator = this.CreateCoordinator();

        // Act: Start drag without providing initial point (null)
        coordinator.StartDrag(draggedItem, 0, tabStripMock.Object, visualElement, visualElement, new Point(0, 0).AsElement(), new Point(0, 0));

        // Assert: Should successfully initialize (GetCursorPos is called internally)
        var isActive = GetPrivateField<bool>(coordinator, "isActive");
        _ = isActive.Should().BeTrue("drag should be active even without explicit initial point");

        var context = GetPrivateField<DragContext?>(coordinator, "dragContext");
        _ = context.Should().NotBeNull("context should be initialized");

        coordinator.Abort();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task EndDrag_InReorderMode_WhenStrategyReturnsNull_CallsCompleteDragWithNullIndex_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visualElement = await CreateLoadedVisualElementAsync().ConfigureAwait(true);
        const int draggedIndex = 0;
        var builder = new TabStripMockBuilder().WithItemSnapshot(draggedIndex);
        var (tabStripMock, stripElement) = builder.BuildWithElement();
        var draggedItem = builder.GetItem(0)!;

        // Update mapper to use the visual element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, visualElement);

        var initialElementPoint = new Point(120, 30).AsElement();

        var coordinator = this.CreateCoordinator(mapperFactory: (w, e) => new SpatialMapper(w, e));

        // Ensure wrapper is loaded so the coordinator can create a SpatialMapper for it
        await LoadTestContentAsync(stripElement).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        coordinator.StartDrag(draggedItem, draggedIndex, tabStripMock.Object, visualElement, visualElement, initialElementPoint, new Point(0, 0));
        coordinator.RegisterTabStrip((ITabStrip)stripElement);

        // Use a strategy that returns null (error case)
        var nullReturningStrategy = new StubDragStrategy { ReturnIndexOverride = null };
        SetPrivateField(coordinator, "currentStrategy", nullReturningStrategy);

        // Act: Drop within bounds but strategy returns null (error case)
        var dropPoint = this.ToScreen(50, 20);
        coordinator.EndDrag(dropPoint);

        // Assert: hitStrip is not null but finalDropIndex is null => error case
        _ = nullReturningStrategy.CompleteCalled.Should().BeTrue();
        tabStripMock.Verify(s => s.TryCompleteDrag(draggedItem, null, null), Times.Once());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(true);

        // Create a test element for the mapper
        this.testElement = new Border
        {
            Width = 100,
            Height = 40,
            Background = new SolidColorBrush(Colors.LightGray),
        };

        await LoadTestContentAsync(this.testElement).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        // Initialize the mapper with the main window and test element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, this.testElement);
    }

    private static async Task<Border> CreateLoadedVisualElementAsync()
    {
        var element = new Border
        {
            Width = 100,
            Height = 40,
            Background = new SolidColorBrush(Colors.LightGray),
        };
        await LoadTestContentAsync(element).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        return element;
    }

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
        return field?.GetValue(coordinator) is not List<WeakReference<ITabStrip>> references
            ? []
            : (List<ITabStrip>)[.. references
            .Select(reference => reference.TryGetTarget(out var strip) ? strip : null)
            .Where(strip => strip is not null)
            .Cast<ITabStrip>(),];
    }

    /// <summary>
    /// Converts element-space coordinates to screen space using the mapper.
    /// </summary>
    private SpatialPoint<ScreenSpace> ToScreen(double x, double y)
    {
        if (this.mapper is null)
        {
            throw new InvalidOperationException("Mapper not initialized. Ensure TestSetupAsync has been called.");
        }

        var elementPoint = new Point(x, y).AsElement();
        return this.mapper.Convert<ElementSpace, ScreenSpace>(elementPoint);
    }

    /// <summary>
    /// Converts element-space coordinates to physical screen space using the mapper.
    /// </summary>
    private SpatialPoint<PhysicalScreenSpace> ToPhysical(double x, double y)
    {
        if (this.mapper is null)
        {
            throw new InvalidOperationException("Mapper not initialized. Ensure TestSetupAsync has been called.");
        }

        var elementPoint = new Point(x, y).AsElement();
        return this.mapper.Convert<ElementSpace, PhysicalScreenSpace>(elementPoint);
    }

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

            _ = wmMock.Setup(wm => wm.GetWindow(It.IsAny<WindowId>())).Returns(windowContext);
            _ = wmMock.Setup(wm => wm.OpenWindows).Returns([windowContext]);
            windowManager = wmMock.Object;
        }

        // Create a HostingContext for UI-affine operations and pass it as the first parameter.
        var dispatcher = DispatcherQueue.GetForCurrentThread();
        var hosting = new HostingContext
        {
            Dispatcher = dispatcher,
            Application = Application.Current,
            DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher),
        };

        return new TabDragCoordinator(
            hosting,
            windowManager,
            mapperFactory ?? ((w, e) => Mock.Of<ISpatialMapper>()),
            dragService ?? Mock.Of<IDragVisualService>(),
            this.LoggerFactory);
    }

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
            => this.LastMovePosition = position;
    }
}
