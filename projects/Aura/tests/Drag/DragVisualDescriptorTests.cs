// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;
using FluentAssertions;
using Windows.Graphics.Imaging;

namespace DroidNet.Aura.Drag.Tests;

/// <summary>
/// Unit tests for <see cref="DragVisualDescriptor"/> class.
/// These are pure unit tests that don't require UI thread.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DragVisualDescriptorTests")]
[TestCategory("UITest")]
public class DragVisualDescriptorTests : VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; }

    /// <summary>
    /// Verifies that descriptor can be created with default values.
    /// HeaderBitmap and PreviewBitmap are optional (service uses placeholder when null).
    /// </summary>
    [TestMethod]
    public void Descriptor_CanBeCreated_WithDefaultValues()
    {
        // Act
        var descriptor = new DragVisualDescriptor();

        // Assert
        _ = descriptor.HeaderBitmap.Should().BeNull("HeaderBitmap should be null by default");
        _ = descriptor.PreviewBitmap.Should().BeNull("PreviewBitmap should be null by default");
        _ = descriptor.RequestedSize.Should().Be(default(Windows.Foundation.Size), "RequestedSize should be default");
    }

    /// <summary>
    /// Verifies that RequestedSize can be set and retrieved.
    /// </summary>
    [TestMethod]
    public void Descriptor_RequestedSize_CanBeSet()
    {
        // Arrange
        var descriptor = new DragVisualDescriptor();
        var expectedSize = new Windows.Foundation.Size(200, 100);

        // Act
        descriptor.RequestedSize = expectedSize;

        // Assert
        _ = descriptor.RequestedSize.Should().Be(expectedSize, "RequestedSize should be set correctly");
    }

    /// <summary>
    /// Verifies that PropertyChanged event is raised when RequestedSize changes.
    /// </summary>
    [TestMethod]
    public void Descriptor_RaisesPropertyChanged_WhenRequestedSizeChanges()
    {
        // Arrange
        var descriptor = new DragVisualDescriptor();
        var eventRaised = false;
        string? propertyName = null;

        descriptor.PropertyChanged += (_, e) =>
        {
            propertyName = e.PropertyName;
            if (string.Equals(e.PropertyName, nameof(DragVisualDescriptor.RequestedSize), StringComparison.Ordinal))
            {
                eventRaised = true;
            }
        };

        // Act
        descriptor.RequestedSize = new Windows.Foundation.Size(300, 150);

        // Assert
        _ = eventRaised.Should().BeTrue("PropertyChanged should be raised for RequestedSize");
        _ = propertyName.Should().Be(nameof(DragVisualDescriptor.RequestedSize), "Property name should match");
    }

    /// <summary>
    /// Verifies that PropertyChanged event is raised when HeaderBitmap changes.
    /// Requires UI thread to create BitmapImage.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    [TestCategory("UITest")]
    public Task Descriptor_RaisesPropertyChanged_WhenHeaderBitmapChanges_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor();
        var eventRaised = false;
        string? propertyName = null;

        descriptor.PropertyChanged += (_, e) =>
        {
            propertyName = e.PropertyName;
            if (string.Equals(e.PropertyName, nameof(DragVisualDescriptor.HeaderBitmap), StringComparison.Ordinal))
            {
                eventRaised = true;
            }
        };

        // Act - Create new SoftwareBitmap to actually change the value
        var newImage = new SoftwareBitmap(BitmapPixelFormat.Bgra8, 1, 1, BitmapAlphaMode.Premultiplied);
        descriptor.HeaderBitmap = newImage;

        // Assert
        _ = eventRaised.Should().BeTrue("PropertyChanged should be raised when HeaderBitmap changes from null to an actual image");
        _ = propertyName.Should().Be(nameof(DragVisualDescriptor.HeaderBitmap), "Property name should match");
        _ = descriptor.HeaderBitmap.Should().BeSameAs(newImage, "HeaderBitmap should be set to the new image");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that PropertyChanged event is raised when PreviewBitmap changes.
    /// Requires UI thread to create BitmapImage.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    [TestCategory("UITest")]
    public Task Descriptor_RaisesPropertyChanged_WhenPreviewBitmapChanges_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor();
        var eventRaised = false;
        string? propertyName = null;

        descriptor.PropertyChanged += (_, e) =>
        {
            propertyName = e.PropertyName;
            if (string.Equals(e.PropertyName, nameof(DragVisualDescriptor.PreviewBitmap), StringComparison.Ordinal))
            {
                eventRaised = true;
            }
        };

        // Act - Create new SoftwareBitmap to actually change the value
        var newImage = new SoftwareBitmap(BitmapPixelFormat.Bgra8, 1, 1, BitmapAlphaMode.Premultiplied);
        descriptor.PreviewBitmap = newImage;

        // Assert
        _ = eventRaised.Should().BeTrue("PropertyChanged should be raised when PreviewBitmap changes from null to an actual image");
        _ = propertyName.Should().Be(nameof(DragVisualDescriptor.PreviewBitmap), "Property name should match");
        _ = descriptor.PreviewBitmap.Should().BeSameAs(newImage, "PreviewBitmap should be set to the new image");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that HeaderBitmap can be set and retrieved correctly.
    /// Requires UI thread to create BitmapImage.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    [TestCategory("UITest")]
    public Task Descriptor_HeaderBitmap_CanBeSet_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor();
        var expectedImage = new SoftwareBitmap(BitmapPixelFormat.Bgra8, 1, 1, BitmapAlphaMode.Premultiplied);

        // Act
        descriptor.HeaderBitmap = expectedImage;

        // Assert
        _ = descriptor.HeaderBitmap.Should().BeSameAs(expectedImage, "HeaderBitmap should be set correctly");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that PreviewBitmap can be set and retrieved correctly.
    /// Requires UI thread to create BitmapImage.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    [TestCategory("UITest")]
    public Task Descriptor_PreviewBitmap_CanBeSet_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor();
        var expectedImage = new SoftwareBitmap(BitmapPixelFormat.Bgra8, 1, 1, BitmapAlphaMode.Premultiplied);

        // Act
        descriptor.PreviewBitmap = expectedImage;

        // Assert
        _ = descriptor.PreviewBitmap.Should().BeSameAs(expectedImage, "PreviewBitmap should be set correctly");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that HeaderBitmap and PreviewBitmap can both be set on the same descriptor.
    /// Requires UI thread to create BitmapImage objects.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    [TestCategory("UITest")]
    public Task Descriptor_CanSetBothHeaderAndPreviewBitmaps_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor();
        var headerImage = new SoftwareBitmap(BitmapPixelFormat.Bgra8, 1, 1, BitmapAlphaMode.Premultiplied);
        var previewImage = new SoftwareBitmap(BitmapPixelFormat.Bgra8, 1, 1, BitmapAlphaMode.Premultiplied);

        // Act
        descriptor.HeaderBitmap = headerImage;
        descriptor.PreviewBitmap = previewImage;

        // Assert
        _ = descriptor.HeaderBitmap.Should().BeSameAs(headerImage, "HeaderBitmap should be set");
        _ = descriptor.PreviewBitmap.Should().BeSameAs(previewImage, "PreviewBitmap should be set");
        _ = descriptor.HeaderBitmap.Should().NotBeSameAs(descriptor.PreviewBitmap, "HeaderBitmap and PreviewBitmap should be different instances");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that setting HeaderBitmap to null raises PropertyChanged.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    [TestCategory("UITest")]
    public Task Descriptor_RaisesPropertyChanged_WhenHeaderBitmapSetToNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { HeaderBitmap = new SoftwareBitmap(BitmapPixelFormat.Bgra8, 1, 1, BitmapAlphaMode.Premultiplied) };
        var eventRaised = false;

        descriptor.PropertyChanged += (_, e) =>
        {
            if (string.Equals(e.PropertyName, nameof(DragVisualDescriptor.HeaderBitmap), StringComparison.Ordinal))
            {
                eventRaised = true;
            }
        };

        // Act - Change from image to null
        descriptor.HeaderBitmap = null;

        // Assert
        _ = eventRaised.Should().BeTrue("PropertyChanged should be raised when HeaderBitmap changes from image to null");
        _ = descriptor.HeaderBitmap.Should().BeNull("HeaderBitmap should be null");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that setting PreviewBitmap to null raises PropertyChanged.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    [TestCategory("UITest")]
    public Task Descriptor_RaisesPropertyChanged_WhenPreviewBitmapSetToNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { PreviewBitmap = new SoftwareBitmap(BitmapPixelFormat.Bgra8, 1, 1, BitmapAlphaMode.Premultiplied) };
        var eventRaised = false;

        descriptor.PropertyChanged += (_, e) =>
        {
            if (string.Equals(e.PropertyName, nameof(DragVisualDescriptor.PreviewBitmap), StringComparison.Ordinal))
            {
                eventRaised = true;
            }
        };

        // Act - Change from image to null
        descriptor.PreviewBitmap = null;

        // Assert
        _ = eventRaised.Should().BeTrue("PropertyChanged should be raised when PreviewBitmap changes from image to null");
        _ = descriptor.PreviewBitmap.Should().BeNull("PreviewBitmap should be null");

        await Task.CompletedTask.ConfigureAwait(true);
    });
}
