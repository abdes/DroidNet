// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Aura.Drag;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI.Xaml.Media.Imaging;

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
    public required TestContext TestContext { get; set; }

    /// <summary>
    /// Verifies that descriptor can be created with default values.
    /// HeaderImage and PreviewImage are optional (service uses placeholder when null).
    /// </summary>
    [TestMethod]
    public void Descriptor_CanBeCreated_WithDefaultValues()
    {
        // Act
        var descriptor = new DragVisualDescriptor();

        // Assert
        _ = descriptor.HeaderImage.Should().BeNull("HeaderImage should be null by default");
        _ = descriptor.PreviewImage.Should().BeNull("PreviewImage should be null by default");
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
    /// Verifies that PropertyChanged event is raised when HeaderImage changes.
    /// Requires UI thread to create BitmapImage.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    [TestCategory("UITest")]
    public Task Descriptor_RaisesPropertyChanged_WhenHeaderImageChanges_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor();
        var eventRaised = false;
        string? propertyName = null;

        descriptor.PropertyChanged += (_, e) =>
        {
            propertyName = e.PropertyName;
            if (string.Equals(e.PropertyName, nameof(DragVisualDescriptor.HeaderImage), StringComparison.Ordinal))
            {
                eventRaised = true;
            }
        };

        // Act - Create new BitmapImage to actually change the value
        var newImage = new BitmapImage();
        descriptor.HeaderImage = newImage;

        // Assert
        _ = eventRaised.Should().BeTrue("PropertyChanged should be raised when HeaderImage changes from null to an actual image");
        _ = propertyName.Should().Be(nameof(DragVisualDescriptor.HeaderImage), "Property name should match");
        _ = descriptor.HeaderImage.Should().BeSameAs(newImage, "HeaderImage should be set to the new image");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that PropertyChanged event is raised when PreviewImage changes.
    /// Requires UI thread to create BitmapImage.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    [TestCategory("UITest")]
    public Task Descriptor_RaisesPropertyChanged_WhenPreviewImageChanges_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor();
        var eventRaised = false;
        string? propertyName = null;

        descriptor.PropertyChanged += (_, e) =>
        {
            propertyName = e.PropertyName;
            if (string.Equals(e.PropertyName, nameof(DragVisualDescriptor.PreviewImage), StringComparison.Ordinal))
            {
                eventRaised = true;
            }
        };

        // Act - Create new BitmapImage to actually change the value
        var newImage = new BitmapImage();
        descriptor.PreviewImage = newImage;

        // Assert
        _ = eventRaised.Should().BeTrue("PropertyChanged should be raised when PreviewImage changes from null to an actual image");
        _ = propertyName.Should().Be(nameof(DragVisualDescriptor.PreviewImage), "Property name should match");
        _ = descriptor.PreviewImage.Should().BeSameAs(newImage, "PreviewImage should be set to the new image");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that HeaderImage can be set and retrieved correctly.
    /// Requires UI thread to create BitmapImage.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    [TestCategory("UITest")]
    public Task Descriptor_HeaderImage_CanBeSet_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor();
        var expectedImage = new BitmapImage();

        // Act
        descriptor.HeaderImage = expectedImage;

        // Assert
        _ = descriptor.HeaderImage.Should().BeSameAs(expectedImage, "HeaderImage should be set correctly");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that PreviewImage can be set and retrieved correctly.
    /// Requires UI thread to create BitmapImage.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    [TestCategory("UITest")]
    public Task Descriptor_PreviewImage_CanBeSet_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor();
        var expectedImage = new BitmapImage();

        // Act
        descriptor.PreviewImage = expectedImage;

        // Assert
        _ = descriptor.PreviewImage.Should().BeSameAs(expectedImage, "PreviewImage should be set correctly");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that HeaderImage and PreviewImage can both be set on the same descriptor.
    /// Requires UI thread to create BitmapImage objects.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    [TestCategory("UITest")]
    public Task Descriptor_CanSetBothHeaderAndPreviewImages_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor();
        var headerImage = new BitmapImage();
        var previewImage = new BitmapImage();

        // Act
        descriptor.HeaderImage = headerImage;
        descriptor.PreviewImage = previewImage;

        // Assert
        _ = descriptor.HeaderImage.Should().BeSameAs(headerImage, "HeaderImage should be set");
        _ = descriptor.PreviewImage.Should().BeSameAs(previewImage, "PreviewImage should be set");
        _ = descriptor.HeaderImage.Should().NotBeSameAs(descriptor.PreviewImage, "HeaderImage and PreviewImage should be different instances");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that setting HeaderImage to null raises PropertyChanged.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    [TestCategory("UITest")]
    public Task Descriptor_RaisesPropertyChanged_WhenHeaderImageSetToNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { HeaderImage = new BitmapImage() };
        var eventRaised = false;

        descriptor.PropertyChanged += (_, e) =>
        {
            if (string.Equals(e.PropertyName, nameof(DragVisualDescriptor.HeaderImage), StringComparison.Ordinal))
            {
                eventRaised = true;
            }
        };

        // Act - Change from image to null
        descriptor.HeaderImage = null;

        // Assert
        _ = eventRaised.Should().BeTrue("PropertyChanged should be raised when HeaderImage changes from image to null");
        _ = descriptor.HeaderImage.Should().BeNull("HeaderImage should be null");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verifies that setting PreviewImage to null raises PropertyChanged.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    [TestCategory("UITest")]
    public Task Descriptor_RaisesPropertyChanged_WhenPreviewImageSetToNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var descriptor = new DragVisualDescriptor { PreviewImage = new BitmapImage() };
        var eventRaised = false;

        descriptor.PropertyChanged += (_, e) =>
        {
            if (string.Equals(e.PropertyName, nameof(DragVisualDescriptor.PreviewImage), StringComparison.Ordinal))
            {
                eventRaised = true;
            }
        };

        // Act - Change from image to null
        descriptor.PreviewImage = null;

        // Assert
        _ = eventRaised.Should().BeTrue("PropertyChanged should be raised when PreviewImage changes from image to null");
        _ = descriptor.PreviewImage.Should().BeNull("PreviewImage should be null");

        await Task.CompletedTask.ConfigureAwait(true);
    });
}
