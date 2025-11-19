// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Linq;
using AwesomeAssertions;
using DroidNet.Aura.Windowing;

namespace DroidNet.Aura.Tests;

/// <summary>
///     Tests for window metadata operations in <see cref="WindowManagerService"/>.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public class WindowManagerServiceTestsMetadata : WindowManagerServiceTestsBase
{
    [TestMethod]
    public Task SetMetadata_WithNewKey_AddsMetadata_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Act
            sut.SetMetadata(context.Id, "TestKey", "TestValue");
            await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            var value = sut.TryGetMetadataValue(context.Id, "TestKey");
            _ = value.Should().Be("TestValue");

            // Verify through context as well
            _ = context.Metadata.Should().NotBeNull();
            _ = context.Metadata.Should().ContainKey("TestKey");
            _ = context.Metadata!["TestKey"].Should().Be("TestValue");
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task SetMetadataAsync_WithExistingKey_UpdatesMetadata_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            sut.SetMetadata(context.Id, "TestKey", "OldValue");

            // Act
            sut.SetMetadata(context.Id, "TestKey", "NewValue");
            await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            var value = sut.TryGetMetadataValue(context.Id, "TestKey");
            _ = value.Should().Be("NewValue");
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task SetMetadataAsync_PublishesMetadataChangedEvent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var changes = new List<WindowMetadataChange>();
        _ = sut.MetadataChanged.Subscribe(changes.Add);

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Act
            sut.SetMetadata(context.Id, "TestKey", "TestValue");
            await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = changes.Should().ContainSingle();
            _ = changes[0].WindowId.Should().Be(context.Id);
            _ = changes[0].Key.Should().Be("TestKey");
            _ = changes[0].OldValue.Should().BeNull();
            _ = changes[0].NewValue.Should().Be("TestValue");
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task SetMetadataAsync_WithSameValue_DoesNotPublishEvent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var changes = new List<WindowMetadataChange>();
        _ = sut.MetadataChanged.Subscribe(changes.Add);

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            sut.SetMetadata(context.Id, "TestKey", "TestValue");
            await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);
            changes.Clear();

            // Act - Set to same value
            sut.SetMetadata(context.Id, "TestKey", "TestValue");
            await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - No new event should be published
            _ = changes.Should().BeEmpty();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task RemoveMetadataAsync_RemovesMetadata_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            sut.SetMetadata(context.Id, "TestKey", "TestValue");

            // Act
            sut.RemoveMetadata(context.Id, "TestKey");
            await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            var value = sut.TryGetMetadataValue(context.Id, "TestKey");
            _ = value.Should().BeNull();

            // Verify through context as well
            _ = context.Metadata.Should().NotContainKey("TestKey");
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task RemoveMetadataAsync_PublishesMetadataChangedEvent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var changes = new List<WindowMetadataChange>();
        _ = sut.MetadataChanged.Subscribe(changes.Add);

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            sut.SetMetadata(context.Id, "TestKey", "TestValue");
            await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);
            changes.Clear();

            // Act
            sut.RemoveMetadata(context.Id, "TestKey");
            await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = changes.Should().ContainSingle();
            _ = changes[0].WindowId.Should().Be(context.Id);
            _ = changes[0].Key.Should().Be("TestKey");
            _ = changes[0].OldValue.Should().Be("TestValue");
            _ = changes[0].NewValue.Should().BeNull();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task TryGetMetadataValueAsync_WithMissingKey_ReturnsNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Act
            var value = sut.TryGetMetadataValue(context.Id, "NonExistentKey");

            // Assert
            _ = value.Should().BeNull();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task TryGetMetadataValueAsync_WithInvalidWindowId_ReturnsNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        try
        {
            var tempWindow = MakeSmallWindow();
            var missingId = tempWindow.AppWindow.Id;
            tempWindow.Close();

            // Act
            var value = sut.TryGetMetadataValue(missingId, "TestKey");

            // Assert
            _ = value.Should().BeNull();
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task SetMetadataAsync_WithInvalidWindowId_DoesNotThrow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        try
        {
            var tempWindow = MakeSmallWindow();
            var missingId = tempWindow.AppWindow.Id;
            tempWindow.Close();

            // Act & Assert - Should complete without throwing
            sut.SetMetadata(missingId, "TestKey", "TestValue");
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task SetMetadataAsync_WithMultipleKeys_ManagesIndependently_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Act
            sut.SetMetadata(context.Id, "Key1", "Value1");
            sut.SetMetadata(context.Id, "Key2", 42);
            sut.SetMetadata(context.Id, "Key3", value: true);
            await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            var value1 = sut.TryGetMetadataValue(context.Id, "Key1");
            var value2 = sut.TryGetMetadataValue(context.Id, "Key2");
            var value3 = sut.TryGetMetadataValue(context.Id, "Key3");

            _ = value1.Should().Be("Value1");
            _ = value2.Should().Be(42);
            _ = value3.Should().Be(expected: true);

            _ = context.Metadata.Should().HaveCount(3);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task SetMetadataAsync_WithComplexObject_StoresCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            var complexObject = new { Name = "Test", Count = 42, Active = true };

            // Act
            sut.SetMetadata(context.Id, "ComplexKey", complexObject);
            await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            var value = sut.TryGetMetadataValue(context.Id, "ComplexKey");
            _ = value.Should().BeSameAs(complexObject);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task Metadata_PreservesInitialMetadata_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();
        var initialMetadata = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["InitialKey1"] = "InitialValue1",
            ["InitialKey2"] = 100,
        };

        var sut = this.CreateService();

        try
        {
            // Act
            var context = await sut.RegisterDecoratedWindowAsync(
                testWindow,
                new("Test"),
                initialMetadata).ConfigureAwait(true);

            // Assert
            _ = context.Metadata.Should().NotBeNull();
            _ = context.Metadata.Should().HaveCount(2);
            _ = context.Metadata!["InitialKey1"].Should().Be("InitialValue1");
            _ = context.Metadata["InitialKey2"].Should().Be(100);

            var value1 = sut.TryGetMetadataValue(context.Id, "InitialKey1");
            var value2 = sut.TryGetMetadataValue(context.Id, "InitialKey2");

            _ = value1.Should().Be("InitialValue1");
            _ = value2.Should().Be(100);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });
}
