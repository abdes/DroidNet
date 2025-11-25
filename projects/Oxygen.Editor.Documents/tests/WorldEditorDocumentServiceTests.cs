// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Moq;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>
/// Unit tests for <see cref="WorldEditorDocumentService"/> core functionality.
/// </summary>
[TestClass]
[TestCategory("Unit")]
[ExcludeFromCodeCoverage]
public class WorldEditorDocumentServiceTests
{
    private WorldEditorDocumentService? service;
    private Mock<ILoggerFactory>? loggerFactoryMock;
    private WindowId testWindowId;

    /// <summary>
    /// Initialize test dependencies before each test.
    /// </summary>
    [TestInitialize]
    public void Initialize()
    {
        this.loggerFactoryMock = new Mock<ILoggerFactory>();
        var mockLogger = new Mock<ILogger<WorldEditorDocumentService>>();
        _ = this.loggerFactoryMock
            .Setup(f => f.CreateLogger(It.IsAny<string>()))
            .Returns(mockLogger.Object);

        this.service = new WorldEditorDocumentService(this.loggerFactoryMock.Object);
        this.testWindowId = new WindowId(1);
    }

    /// <summary>
    /// Test that opening a document with valid metadata returns the correct document ID.
    /// </summary>
    [TestMethod]
    public async Task OpenDocumentAsync_WithValidMetadata_ReturnsDocumentId()
    {
        // Arrange
        var expectedId = Guid.NewGuid();
        var metadata = new SceneDocumentMetadata(expectedId)
        {
            Title = "Test Scene",
        };

        // Act
        var result = await this.service!.OpenDocumentAsync(
            this.testWindowId,
            metadata,
            indexHint: -1,
            shouldSelect: true).ConfigureAwait(false);

        // Assert
        _ = result.Should().Be(expectedId);
    }

    /// <summary>
    /// Test that opening a document with empty GUID generates a new GUID.
    /// </summary>
    [TestMethod]
    public async Task OpenDocumentAsync_WithEmptyGuid_GeneratesNewGuid()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata
        {
            Title = "Test Scene",
        };

        // Act
        var result = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Assert
        _ = result.Should().NotBe(Guid.Empty);
    }

    /// <summary>
    /// Test that opening a document adds it to the window's document collection.
    /// </summary>
    [TestMethod]
    public async Task OpenDocumentAsync_AddsDocumentToWindowCollection()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };

        // Act
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Assert
        var openDocs = this.service.GetOpenDocuments(this.testWindowId);
        _ = openDocs.Count.Should().Be(1);
        _ = openDocs[0].DocumentId.Should().Be(docId);
    }

    /// <summary>
    /// Test that opening a document with shouldSelect=true sets it as active.
    /// </summary>
    [TestMethod]
    public async Task OpenDocumentAsync_WithShouldSelectTrue_SetsActiveDocument()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };

        // Act
        var docId = await this.service!.OpenDocumentAsync(
            this.testWindowId,
            metadata,
            shouldSelect: true).ConfigureAwait(false);

        // Assert
        var activeId = this.service.GetActiveDocumentId(this.testWindowId);
        _ = activeId.Should().NotBeNull();
        _ = activeId!.Value.Should().Be(docId);
    }

    /// <summary>
    /// Test that opening a document with shouldSelect=false does not set it as active.
    /// </summary>
    [TestMethod]
    public async Task OpenDocumentAsync_WithShouldSelectFalse_DoesNotSetActiveDocument()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };

        // Act
        _ = await this.service!.OpenDocumentAsync(
            this.testWindowId,
            metadata,
            shouldSelect: false).ConfigureAwait(false);

        // Assert
        var activeId = this.service.GetActiveDocumentId(this.testWindowId);
        _ = activeId.Should().BeNull();
    }

    /// <summary>
    /// Test that opening a second scene document closes the first one.
    /// </summary>
    [TestMethod]
    public async Task OpenDocumentAsync_WithSecondSceneDocument_ClosesFirstScene()
    {
        // Arrange
        var firstScene = new SceneDocumentMetadata { Title = "Scene 1" };
        var secondScene = new SceneDocumentMetadata { Title = "Scene 2" };

        _ = await this.service!.OpenDocumentAsync(this.testWindowId, firstScene).ConfigureAwait(false);

        // Act
        var secondId = await this.service.OpenDocumentAsync(this.testWindowId, secondScene).ConfigureAwait(false);

        // Assert
        var openDocs = this.service.GetOpenDocuments(this.testWindowId);
        _ = openDocs.Count.Should().Be(1);
        _ = openDocs[0].Title.Should().Be("Scene 2");
        _ = openDocs[0].DocumentId.Should().Be(secondId);
    }

    /// <summary>
    /// Test that opening a second scene document when first close is vetoed returns Guid.Empty.
    /// </summary>
    [TestMethod]
    public async Task OpenDocumentAsync_WhenFirstSceneCloseVetoed_ReturnsEmptyGuid()
    {
        // Arrange
        var firstScene = new SceneDocumentMetadata { Title = "Scene 1" };
        var secondScene = new SceneDocumentMetadata { Title = "Scene 2" };

        _ = await this.service!.OpenDocumentAsync(this.testWindowId, firstScene).ConfigureAwait(false);

        // Add veto handler
        this.service.DocumentClosing += (sender, args)
            => args.AddVetoTask(Task.FromResult(false));

        // Act
        var result = await this.service.OpenDocumentAsync(this.testWindowId, secondScene).ConfigureAwait(false);

        // Assert
        _ = result.Should().Be(Guid.Empty);
        var openDocs = this.service.GetOpenDocuments(this.testWindowId);
        _ = openDocs.Count.Should().Be(1);
        _ = openDocs[0].Title.Should().Be("Scene 1");
    }

    /// <summary>
    /// Test that closing an existing document returns true.
    /// </summary>
    [TestMethod]
    public async Task CloseDocumentAsync_WithExistingDocument_ReturnsTrue()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Act
        var result = await this.service.CloseDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeTrue();
    }

    /// <summary>
    /// Test that closing a document removes it from the collection.
    /// </summary>
    [TestMethod]
    public async Task CloseDocumentAsync_RemovesDocumentFromCollection()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Act
        _ = await this.service.CloseDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        var openDocs = this.service.GetOpenDocuments(this.testWindowId);
        _ = openDocs.Count.Should().Be(0);
    }

    /// <summary>
    /// Test that closing a non-existent document returns false.
    /// </summary>
    [TestMethod]
    public async Task CloseDocumentAsync_WithNonExistentDocument_ReturnsFalse()
    {
        // Arrange
        var nonExistentId = Guid.NewGuid();

        // Act
        var result = await this.service!.CloseDocumentAsync(this.testWindowId, nonExistentId).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeFalse();
    }

    /// <summary>
    /// Test that closing with veto returns false and document remains open.
    /// </summary>
    [TestMethod]
    public async Task CloseDocumentAsync_WithVeto_ReturnsFalseAndDocumentRemainsOpen()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        this.service.DocumentClosing += (sender, args) => args.AddVetoTask(Task.FromResult(false));

        // Act
        var result = await this.service.CloseDocumentAsync(this.testWindowId, docId, force: false).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeFalse();
        var openDocs = this.service.GetOpenDocuments(this.testWindowId);
        _ = openDocs.Count.Should().Be(1);
    }

    /// <summary>
    /// Test that closing with force=true bypasses veto.
    /// </summary>
    [TestMethod]
    public async Task CloseDocumentAsync_WithForceTrue_BypassesVeto()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        this.service.DocumentClosing += (sender, args)
            => args.AddVetoTask(Task.FromResult(false));

        // Act
        var result = await this.service.CloseDocumentAsync(this.testWindowId, docId, force: true).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeTrue();
        var openDocs = this.service.GetOpenDocuments(this.testWindowId);
        _ = openDocs.Count.Should().Be(0);
    }

    /// <summary>
    /// Test that closing the active document clears the active document ID.
    /// </summary>
    [TestMethod]
    public async Task CloseDocumentAsync_ClearsActiveDocumentId()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata, shouldSelect: true).ConfigureAwait(false);

        // Act
        _ = await this.service.CloseDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        var activeId = this.service.GetActiveDocumentId(this.testWindowId);
        _ = activeId.Should().BeNull();
    }

    /// <summary>
    /// Test that detaching an existing document returns its metadata.
    /// </summary>
    [TestMethod]
    public async Task DetachDocumentAsync_WithExistingDocument_ReturnsMetadata()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Act
        var result = await this.service.DetachDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        _ = result.Should().NotBeNull();
        _ = result!.DocumentId.Should().Be(docId);
    }

    /// <summary>
    /// Test that detaching a document removes it from the collection.
    /// </summary>
    [TestMethod]
    public async Task DetachDocumentAsync_RemovesDocumentFromCollection()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Act
        _ = await this.service.DetachDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        var openDocs = this.service.GetOpenDocuments(this.testWindowId);
        _ = openDocs.Count.Should().Be(0);
    }

    /// <summary>
    /// Test that detaching a non-existent document returns null.
    /// </summary>
    [TestMethod]
    public async Task DetachDocumentAsync_WithNonExistentDocument_ReturnsNull()
    {
        // Arrange
        var nonExistentId = Guid.NewGuid();

        // Act
        var result = await this.service!.DetachDocumentAsync(this.testWindowId, nonExistentId).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeNull();
    }

    /// <summary>
    /// Test that attaching a document returns true.
    /// </summary>
    [TestMethod]
    public async Task AttachDocumentAsync_WithValidMetadata_ReturnsTrue()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata
        {
            Title = "Test Scene",
        };

        // Act
        var result = await this.service!.AttachDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeTrue();
    }

    /// <summary>
    /// Test that attaching a document adds it to the window's collection.
    /// </summary>
    [TestMethod]
    public async Task AttachDocumentAsync_AddsDocumentToCollection()
    {
        // Arrange
        var docId = Guid.NewGuid();
        var metadata = new SceneDocumentMetadata(docId)
        {
            Title = "Test Scene",
        };

        // Act
        _ = await this.service!.AttachDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Assert
        var openDocs = this.service.GetOpenDocuments(this.testWindowId);
        _ = openDocs.Count.Should().Be(1);
        _ = openDocs[0].DocumentId.Should().Be(docId);
    }

    /// <summary>
    /// Test that attaching a scene document when one exists closes the existing one.
    /// </summary>
    [TestMethod]
    public async Task AttachDocumentAsync_WithExistingScene_ClosesExistingScene()
    {
        // Arrange
        var firstScene = new SceneDocumentMetadata
        {
            Title = "Scene 1",
        };
        var secondScene = new SceneDocumentMetadata
        {
            Title = "Scene 2",
        };

        _ = await this.service!.OpenDocumentAsync(this.testWindowId, firstScene).ConfigureAwait(false);

        // Act
        _ = await this.service.AttachDocumentAsync(this.testWindowId, secondScene).ConfigureAwait(false);

        // Assert
        var openDocs = this.service.GetOpenDocuments(this.testWindowId);
        _ = openDocs.Count.Should().Be(1);
        _ = openDocs[0].Title.Should().Be("Scene 2");
    }

    /// <summary>
    /// Test that attaching when existing scene close is vetoed returns false.
    /// </summary>
    [TestMethod]
    public async Task AttachDocumentAsync_WhenExistingSceneCloseVetoed_ReturnsFalse()
    {
        // Arrange
        var firstScene = new SceneDocumentMetadata { Title = "Scene 1" };
        var secondScene = new SceneDocumentMetadata
        {
            Title = "Scene 2",
        };

        _ = await this.service!.OpenDocumentAsync(this.testWindowId, firstScene).ConfigureAwait(false);

        this.service.DocumentClosing += (sender, args)
            => args.AddVetoTask(Task.FromResult(false));

        // Act
        var result = await this.service.AttachDocumentAsync(this.testWindowId, secondScene).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeFalse();
        var openDocs = this.service.GetOpenDocuments(this.testWindowId);
        _ = openDocs.Count.Should().Be(1);
    }

    /// <summary>
    /// Test that updating metadata for an existing document returns true.
    /// </summary>
    [TestMethod]
    public async Task UpdateMetadataAsync_WithExistingDocument_ReturnsTrue()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Original Title" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        var updatedMetadata = new SceneDocumentMetadata(docId)
        {
            Title = "Updated Title",
        };

        // Act
        var result = await this.service.UpdateMetadataAsync(this.testWindowId, docId, updatedMetadata).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeTrue();
    }

    /// <summary>
    /// Test that updating metadata actually updates the stored metadata.
    /// </summary>
    [TestMethod]
    public async Task UpdateMetadataAsync_UpdatesStoredMetadata()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Original Title" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        var updatedMetadata = new SceneDocumentMetadata(docId)
        {
            Title = "Updated Title",
        };

        // Act
        _ = await this.service.UpdateMetadataAsync(this.testWindowId, docId, updatedMetadata).ConfigureAwait(false);

        // Assert
        var openDocs = this.service.GetOpenDocuments(this.testWindowId);
        _ = openDocs[0].Title.Should().Be("Updated Title");
    }

    /// <summary>
    /// Test that updating metadata for a non-existent document returns false.
    /// </summary>
    [TestMethod]
    public async Task UpdateMetadataAsync_WithNonExistentDocument_ReturnsFalse()
    {
        // Arrange
        var nonExistentId = Guid.NewGuid();
        var metadata = new SceneDocumentMetadata(nonExistentId)
        {
            Title = "Test",
        };

        // Act
        var result = await this.service!.UpdateMetadataAsync(this.testWindowId, nonExistentId, metadata).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeFalse();
    }

    /// <summary>
    /// Test that selecting an existing document returns true.
    /// </summary>
    [TestMethod]
    public async Task SelectDocumentAsync_WithExistingDocument_ReturnsTrue()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata, shouldSelect: false).ConfigureAwait(false);

        // Act
        var result = await this.service.SelectDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeTrue();
    }

    /// <summary>
    /// Test that selecting a document sets it as active.
    /// </summary>
    [TestMethod]
    public async Task SelectDocumentAsync_SetsDocumentAsActive()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata, shouldSelect: false).ConfigureAwait(false);

        // Act
        _ = await this.service.SelectDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        var activeId = this.service.GetActiveDocumentId(this.testWindowId);
        _ = activeId.Should().NotBeNull();
        _ = activeId!.Value.Should().Be(docId);
    }

    /// <summary>
    /// Test that selecting a non-existent document returns false.
    /// </summary>
    [TestMethod]
    public async Task SelectDocumentAsync_WithNonExistentDocument_ReturnsFalse()
    {
        // Arrange
        var nonExistentId = Guid.NewGuid();

        // Act
        var result = await this.service!.SelectDocumentAsync(this.testWindowId, nonExistentId).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeFalse();
    }

    /// <summary>
    /// Test that GetOpenDocuments returns empty collection for window with no documents.
    /// </summary>
    [TestMethod]
    public void GetOpenDocuments_WithNoDocuments_ReturnsEmptyCollection()
    {
        // Act
        var result = this.service!.GetOpenDocuments(this.testWindowId);

        // Assert
        _ = result.Should().NotBeNull();
        _ = result.Count.Should().Be(0);
    }

    /// <summary>
    /// Test that GetOpenDocuments returns all open documents for a window.
    /// </summary>
    [TestMethod]
    public async Task GetOpenDocuments_ReturnsAllOpenDocuments()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        _ = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Act
        var result = this.service.GetOpenDocuments(this.testWindowId);

        // Assert
        _ = result.Count.Should().Be(1);
    }

    /// <summary>
    /// Test that GetActiveDocumentId returns null when no document is active.
    /// </summary>
    [TestMethod]
    public void GetActiveDocumentId_WithNoActiveDocument_ReturnsNull()
    {
        // Act
        var result = this.service!.GetActiveDocumentId(this.testWindowId);

        // Assert
        _ = result.Should().BeNull();
    }

    /// <summary>
    /// Test that GetActiveDocumentId returns the active document ID.
    /// </summary>
    [TestMethod]
    public async Task GetActiveDocumentId_ReturnsActiveDocumentId()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Test Scene" };
        var docId = await this.service!.OpenDocumentAsync(this.testWindowId, metadata, shouldSelect: true).ConfigureAwait(false);

        // Act
        var result = this.service.GetActiveDocumentId(this.testWindowId);

        // Assert
        _ = result.Should().NotBeNull();
        _ = result!.Value.Should().Be(docId);
    }
}
