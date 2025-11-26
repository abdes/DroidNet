// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Moq;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>
/// Unit tests for <see cref="EditorDocumentService"/> event raising behavior.
/// </summary>
[TestClass]
[TestCategory("Unit")]
[ExcludeFromCodeCoverage]
public class EditorDocumentServiceEventTests
{
    private EditorDocumentService? service;
    private WindowId testWindowId;

    /// <summary>
    /// Initialize test dependencies before each test.
    /// </summary>
    [TestInitialize]
    public void Initialize()
    {
        var loggerFactoryMock = new Mock<ILoggerFactory>();
        var mockLogger = new Mock<ILogger<EditorDocumentService>>();
        _ = loggerFactoryMock
            .Setup(f => f.CreateLogger(It.IsAny<string>()))
            .Returns(mockLogger.Object);

        this.service = new EditorDocumentService(loggerFactoryMock.Object);
        this.testWindowId = new WindowId(1);
    }

    /// <summary>
    /// Test that DocumentOpened event is raised when opening a document.
    /// </summary>
    [TestMethod]
    public async Task OpenDocumentAsync_RaisesDocumentOpenedEvent()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var eventRaised = false;

        this.service!.DocumentOpened += (sender, args) => eventRaised = true;

        // Act
        _ = await this.service.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Assert
        _ = eventRaised.Should().BeTrue();
    }

    /// <summary>
    /// Test that DocumentOpened event contains correct window ID.
    /// </summary>
    [TestMethod]
    public async Task OpenDocumentAsync_DocumentOpenedEvent_ContainsCorrectWindowId()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        WindowId? capturedWindowId = null;

        this.service!.DocumentOpened += (sender, args) => capturedWindowId = args.WindowId;

        // Act
        _ = await this.service.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Assert
        _ = capturedWindowId.Should().NotBeNull();
        _ = capturedWindowId.Value.Should().Be(this.testWindowId);
    }

    /// <summary>
    /// Test that DocumentOpened event contains correct metadata.
    /// </summary>
    [TestMethod]
    public async Task OpenDocumentAsync_DocumentOpenedEvent_ContainsCorrectMetadata()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        IDocumentMetadata? capturedMetadata = null;

        this.service!.DocumentOpened += (sender, args) => capturedMetadata = args.Metadata;

        // Act
        _ = await this.service.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Assert
        _ = capturedMetadata.Should().NotBeNull();
        _ = capturedMetadata.Should().BeSameAs(metadata);
    }

    /// <summary>
    /// Test that DocumentOpened event contains correct indexHint.
    /// </summary>
    [TestMethod]
    public async Task OpenDocumentAsync_DocumentOpenedEvent_ContainsCorrectIndexHint()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        const int expectedIndexHint = 5;
        int? capturedIndexHint = null;

        this.service!.DocumentOpened += (sender, args) => capturedIndexHint = args.IndexHint;

        // Act
        _ = await this.service.OpenDocumentAsync(this.testWindowId, metadata, indexHint: expectedIndexHint).ConfigureAwait(false);

        // Assert
        _ = capturedIndexHint.Should().NotBeNull();
        _ = capturedIndexHint!.Value.Should().Be(expectedIndexHint);
    }

    /// <summary>
    /// Test that DocumentOpened event contains correct shouldSelect flag.
    /// </summary>
    [TestMethod]
    public async Task OpenDocumentAsync_DocumentOpenedEvent_ContainsCorrectShouldSelect()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        bool? capturedShouldSelect = null;

        this.service!.DocumentOpened += (sender, args) => capturedShouldSelect = args.ShouldSelect;

        // Act
        _ = await this.service.OpenDocumentAsync(this.testWindowId, metadata, shouldSelect: true).ConfigureAwait(false);

        // Assert
        _ = capturedShouldSelect.Should().NotBeNull();
        _ = capturedShouldSelect!.Value.Should().BeTrue();
    }

    /// <summary>
    /// Test that DocumentClosing event is raised when closing a document.
    /// </summary>
    [TestMethod]
    public async Task CloseDocumentAsync_RaisesDocumentClosingEvent()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        var eventRaised = false;

        this.service.DocumentClosing += (sender, args) => eventRaised = true;

        // Act
        _ = await this.service.CloseDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        _ = eventRaised.Should().BeTrue();
    }

    /// <summary>
    /// Test that DocumentClosing event contains correct force flag.
    /// </summary>
    [TestMethod]
    public async Task CloseDocumentAsync_DocumentClosingEvent_ContainsCorrectForceFlag()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        bool? capturedForce = null;

        this.service.DocumentClosing += (sender, args) => capturedForce = args.Force;

        // Act
        _ = await this.service.CloseDocumentAsync(this.testWindowId, docId, force: true).ConfigureAwait(false);

        // Assert
        _ = capturedForce.Should().NotBeNull();
        _ = capturedForce!.Value.Should().BeTrue();
    }

    /// <summary>
    /// Test that DocumentClosing event is raised even with force=true.
    /// </summary>
    [TestMethod]
    public async Task CloseDocumentAsync_WithForceTrue_StillRaisesDocumentClosingEvent()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        var eventRaised = false;

        this.service.DocumentClosing += (sender, args) =>
        {
            eventRaised = true;
            args.AddVetoTask(Task.FromResult(false)); // Try to veto (should be ignored)
        };

        // Act
        _ = await this.service.CloseDocumentAsync(this.testWindowId, docId, force: true).ConfigureAwait(false);

        // Assert
        _ = eventRaised.Should().BeTrue();
    }

    /// <summary>
    /// Test that DocumentClosed event is raised after closing a document.
    /// </summary>
    [TestMethod]
    public async Task CloseDocumentAsync_RaisesDocumentClosedEvent()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        var eventRaised = false;

        this.service.DocumentClosed += (sender, args) => eventRaised = true;

        // Act
        _ = await this.service.CloseDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        _ = eventRaised.Should().BeTrue();
    }

    /// <summary>
    /// Test that DocumentClosed event contains correct metadata.
    /// </summary>
    [TestMethod]
    public async Task CloseDocumentAsync_DocumentClosedEvent_ContainsCorrectMetadata()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        IDocumentMetadata? capturedMetadata = null;

        this.service.DocumentClosed += (sender, args) => capturedMetadata = args.Metadata;

        // Act
        _ = await this.service.CloseDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        _ = capturedMetadata.Should().NotBeNull();
        _ = capturedMetadata!.Title.Should().Be("Test Scene");
    }

    /// <summary>
    /// Test that DocumentClosed event is NOT raised when close is vetoed.
    /// </summary>
    [TestMethod]
    public async Task CloseDocumentAsync_WhenVetoed_DoesNotRaiseDocumentClosedEvent()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        var closedEventRaised = false;

        this.service.DocumentClosing += (sender, args)
            => args.AddVetoTask(Task.FromResult(false));

        this.service.DocumentClosed += (sender, args) => closedEventRaised = true;

        // Act
        _ = await this.service.CloseDocumentAsync(this.testWindowId, docId, force: false).ConfigureAwait(false);

        // Assert
        _ = closedEventRaised.Should().BeFalse();
    }

    /// <summary>
    /// Test that DocumentDetached event is raised when detaching a document.
    /// </summary>
    [TestMethod]
    public async Task DetachDocumentAsync_RaisesDocumentDetachedEvent()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        var eventRaised = false;

        this.service.DocumentDetached += (sender, args) => eventRaised = true;

        // Act
        _ = await this.service.DetachDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        _ = eventRaised.Should().BeTrue();
    }

    /// <summary>
    /// Test that DocumentDetached event contains correct metadata.
    /// </summary>
    [TestMethod]
    public async Task DetachDocumentAsync_DocumentDetachedEvent_ContainsCorrectMetadata()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        IDocumentMetadata? capturedMetadata = null;

        this.service.DocumentDetached += (sender, args) => capturedMetadata = args.Metadata;

        // Act
        _ = await this.service.DetachDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        _ = capturedMetadata.Should().NotBeNull();
        _ = capturedMetadata!.DocumentId.Should().Be(docId);
    }

    /// <summary>
    /// Test that DocumentDetached event is NOT raised for non-existent document.
    /// </summary>
    [TestMethod]
    public async Task DetachDocumentAsync_WithNonExistentDocument_DoesNotRaiseEvent()
    {
        // Arrange
        var nonExistentId = Guid.NewGuid();
        var eventRaised = false;

        this.service!.DocumentDetached += (sender, args) => eventRaised = true;

        // Act
        _ = await this.service.DetachDocumentAsync(this.testWindowId, nonExistentId).ConfigureAwait(false);

        // Assert
        _ = eventRaised.Should().BeFalse();
    }

    /// <summary>
    /// Test that DocumentAttached event is raised when attaching a document.
    /// </summary>
    [TestMethod]
    public async Task AttachDocumentAsync_RaisesDocumentAttachedEvent()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata
        {
            Title = "Test Scene",
        };
        var eventRaised = false;

        this.service!.DocumentAttached += (sender, args) => eventRaised = true;

        // Act
        _ = await this.service.AttachDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Assert
        _ = eventRaised.Should().BeTrue();
    }

    /// <summary>
    /// Test that DocumentAttached event contains correct metadata.
    /// </summary>
    [TestMethod]
    public async Task AttachDocumentAsync_DocumentAttachedEvent_ContainsCorrectMetadata()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata
        {
            Title = "Test Scene",
        };
        IDocumentMetadata? capturedMetadata = null;

        this.service!.DocumentAttached += (sender, args) => capturedMetadata = args.Metadata;

        // Act
        _ = await this.service.AttachDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Assert
        _ = capturedMetadata.Should().NotBeNull();
        _ = capturedMetadata.Should().BeSameAs(metadata);
    }

    /// <summary>
    /// Test that DocumentAttached event contains correct indexHint.
    /// </summary>
    [TestMethod]
    public async Task AttachDocumentAsync_DocumentAttachedEvent_ContainsCorrectIndexHint()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata
        {
            Title = "Test Scene",
        };
        const int expectedIndexHint = 3;
        int? capturedIndexHint = null;

        this.service!.DocumentAttached += (sender, args) => capturedIndexHint = args.IndexHint;

        // Act
        _ = await this.service.AttachDocumentAsync(this.testWindowId, metadata, indexHint: expectedIndexHint).ConfigureAwait(false);

        // Assert
        _ = capturedIndexHint.Should().NotBeNull();
        _ = capturedIndexHint!.Value.Should().Be(expectedIndexHint);
    }

    /// <summary>
    /// Test that DocumentMetadataChanged event is raised when updating metadata.
    /// </summary>
    [TestMethod]
    public async Task UpdateMetadataAsync_RaisesDocumentMetadataChangedEvent()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Original Title" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        var eventRaised = false;

        this.service.DocumentMetadataChanged += (sender, args) => eventRaised = true;

        var updatedMetadata = new SceneDocumentMetadata(docId)
        {
            Title = "Updated Title",
        };

        // Act
        _ = await this.service.UpdateMetadataAsync(this.testWindowId, docId, updatedMetadata).ConfigureAwait(false);

        // Assert
        _ = eventRaised.Should().BeTrue();
    }

    /// <summary>
    /// Test that DocumentMetadataChanged event contains new metadata.
    /// </summary>
    [TestMethod]
    public async Task UpdateMetadataAsync_DocumentMetadataChangedEvent_ContainsNewMetadata()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Original Title" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        IDocumentMetadata? capturedMetadata = null;

        this.service.DocumentMetadataChanged += (sender, args) => capturedMetadata = args.NewMetadata;

        var updatedMetadata = new SceneDocumentMetadata(docId)
        {
            Title = "Updated Title",
        };

        // Act
        _ = await this.service.UpdateMetadataAsync(this.testWindowId, docId, updatedMetadata).ConfigureAwait(false);

        // Assert
        _ = capturedMetadata.Should().NotBeNull();
        _ = capturedMetadata!.Title.Should().Be("Updated Title");
    }

    /// <summary>
    /// Test that DocumentMetadataChanged event is NOT raised for non-existent document.
    /// </summary>
    [TestMethod]
    public async Task UpdateMetadataAsync_WithNonExistentDocument_DoesNotRaiseEvent()
    {
        // Arrange
        var nonExistentId = Guid.NewGuid();
        var metadata = new SceneDocumentMetadata(nonExistentId)
        {
            Title = "Test",
        };
        var eventRaised = false;

        this.service!.DocumentMetadataChanged += (sender, args) => eventRaised = true;

        // Act
        _ = await this.service.UpdateMetadataAsync(this.testWindowId, nonExistentId, metadata).ConfigureAwait(false);

        // Assert
        _ = eventRaised.Should().BeFalse();
    }

    /// <summary>
    /// Test that DocumentActivated event is raised when selecting a document.
    /// </summary>
    [TestMethod]
    public async Task SelectDocumentAsync_RaisesDocumentActivatedEvent()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata, shouldSelect: false).ConfigureAwait(false);
        var eventRaised = false;

        this.service.DocumentActivated += (sender, args) => eventRaised = true;

        // Act
        _ = await this.service.SelectDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        _ = eventRaised.Should().BeTrue();
    }

    /// <summary>
    /// Test that DocumentActivated event contains correct document ID.
    /// </summary>
    [TestMethod]
    public async Task SelectDocumentAsync_DocumentActivatedEvent_ContainsCorrectDocumentId()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata, shouldSelect: false).ConfigureAwait(false);
        Guid? capturedDocId = null;

        this.service.DocumentActivated += (sender, args) => capturedDocId = args.DocumentId;

        // Act
        _ = await this.service.SelectDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        _ = capturedDocId.Should().NotBeNull();
        _ = capturedDocId!.Value.Should().Be(docId);
    }

    /// <summary>
    /// Test that DocumentActivated event is NOT raised for non-existent document.
    /// </summary>
    [TestMethod]
    public async Task SelectDocumentAsync_WithNonExistentDocument_DoesNotRaiseEvent()
    {
        // Arrange
        var nonExistentId = Guid.NewGuid();
        var eventRaised = false;

        this.service!.DocumentActivated += (sender, args) => eventRaised = true;

        // Act
        _ = await this.service.SelectDocumentAsync(this.testWindowId, nonExistentId).ConfigureAwait(false);

        // Assert
        _ = eventRaised.Should().BeFalse();
    }

    /// <summary>
    /// Test that DocumentActivated event is raised when opening with shouldSelect=true.
    /// </summary>
    [TestMethod]
    public async Task OpenDocumentAsync_WithShouldSelectTrue_RaisesDocumentActivatedEvent()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var eventRaised = false;

        this.service!.DocumentActivated += (sender, args) => eventRaised = true;

        // Act
        _ = await this.service.OpenDocumentAsync(this.testWindowId, metadata, shouldSelect: true).ConfigureAwait(false);

        // Assert
        _ = eventRaised.Should().BeTrue();
    }
}
