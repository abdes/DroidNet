// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using DroidNet.Aura.Windowing;
using DroidNet.Coordinates;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
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
public partial class TabDragCoordinatorTests : VisualUserInterfaceTests
{
    private SpatialMapper? mapper;

    private Border? testElement;

    public TestContext TestContext { get; set; }

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
            var windowContext = new ManagedWindow
            {
                DispatcherQueue = VisualUserInterfaceTestsApp.DispatcherQueue,
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
