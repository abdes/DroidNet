// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

/// <summary>
/// Unit tests for <see cref="DocumentTabs"/> core functionality.
/// </summary>
[TestClass]
[TestCategory("Unit")]
[ExcludeFromCodeCoverage]
public class DocumentTabsTests
{
    private DocumentTabs? service;
    private Mock<ILoggerFactory>? loggerFactoryMock;
    private WindowId testWindowId;

    /// <summary>
    /// Initialize test dependencies before each test.
    /// </summary>
    [TestInitialize]
    public void Initialize()
    {
        this.loggerFactoryMock = new Mock<ILoggerFactory>();
        var mockLogger = new Mock<ILogger<DocumentTabs>>();
        _ = this.loggerFactoryMock
            .Setup(f => f.CreateLogger(It.IsAny<string>()))
            .Returns(mockLogger.Object);

        this.service = Tabs.New(this.loggerFactoryMock.Object);
        this.testWindowId = new WindowId(1);
    }

    /// <summary>
    /// Test that opening a document keys its tab by the metadata's document id.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task OpenDocumentAsync_WithValidMetadata_KeysTheTabByItsDocumentId()
    {
        // Arrange
        var expectedId = DocumentId.New();
        var metadata = new TestDocumentMetadata(expectedId)
        {
            Title = "Test Document",
        };

        // Act
        var result = await this.service!.OpenDocumentAsync(
            this.testWindowId,
            metadata,
            indexHint: -1,
            shouldSelect: true).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeTrue();
        _ = this.service.GetActiveDocumentId(this.testWindowId).Should().Be(expectedId);
    }

    /// <summary>
    /// Test that opening a document adds it to the window's document collection.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task OpenDocumentAsync_AddsDocumentToWindowCollection()
    {
        // Arrange
        var metadata = new TestDocumentMetadata { Title = "Test Document" };

        // Act
        _ = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        var docId = metadata.DocumentId;

        // Assert
        var openDocs = this.service.GetOpenDocuments(this.testWindowId);
        _ = openDocs.Count.Should().Be(1);
        _ = openDocs[0].DocumentId.Should().Be(docId);
    }

    /// <summary>
    /// Test that opening a document with shouldSelect=true sets it as active.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task OpenDocumentAsync_WithShouldSelectTrue_SetsActiveDocument()
    {
        // Arrange
        var metadata = new TestDocumentMetadata { Title = "Test Document" };

        // Act
        _ = await this.service!.OpenDocumentAsync(
            this.testWindowId,
            metadata,
            shouldSelect: true).ConfigureAwait(false);
        var docId = metadata.DocumentId;

        // Assert
        var activeId = this.service.GetActiveDocumentId(this.testWindowId);
        _ = activeId.Should().NotBeNull();
        _ = activeId!.Value.Should().Be(docId);
    }

    /// <summary>
    /// Test that opening a document with shouldSelect=false does not set it as active.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task OpenDocumentAsync_WithShouldSelectFalse_DoesNotSetActiveDocument()
    {
        // Arrange
        var metadata = new TestDocumentMetadata { Title = "Test Document" };

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
    /// Test that closing an existing document returns true.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CloseDocumentAsync_WithExistingDocument_ReturnsTrue()
    {
        // Arrange
        var metadata = new TestDocumentMetadata { Title = "Test Document" };
        _ = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        var docId = metadata.DocumentId;

        // Act
        var result = await this.service.CloseDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeTrue();
    }

    /// <summary>
    /// Test that closing a document removes it from the collection.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CloseDocumentAsync_RemovesDocumentFromCollection()
    {
        // Arrange
        var metadata = new TestDocumentMetadata { Title = "Test Document" };
        _ = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        var docId = metadata.DocumentId;

        // Act
        _ = await this.service.CloseDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        var openDocs = this.service.GetOpenDocuments(this.testWindowId);
        _ = openDocs.Count.Should().Be(0);
    }

    /// <summary>
    /// Test that closing a non-existent document returns false.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CloseDocumentAsync_WithNonExistentDocument_ReturnsFalse()
    {
        // Arrange
        var nonExistentId = DocumentId.New();

        // Act
        var result = await this.service!.CloseDocumentAsync(this.testWindowId, nonExistentId).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeFalse();
    }

    /// <summary>
    /// Test that closing with veto returns false and document remains open.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CloseDocumentAsync_WithVeto_ReturnsFalseAndDocumentRemainsOpen()
    {
        // Arrange
        var metadata = new TestDocumentMetadata { Title = "Test Document" };
        _ = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        var docId = metadata.DocumentId;

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
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CloseDocumentAsync_WithForceTrue_BypassesVeto()
    {
        // Arrange
        var metadata = new TestDocumentMetadata { Title = "Test Document" };
        _ = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        var docId = metadata.DocumentId;

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
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CloseDocumentAsync_ClearsActiveDocumentId()
    {
        // Arrange
        var metadata = new TestDocumentMetadata { Title = "Test Document" };
        _ = await this.service!.OpenDocumentAsync(this.testWindowId, metadata, shouldSelect: true).ConfigureAwait(false);
        var docId = metadata.DocumentId;

        // Act
        _ = await this.service.CloseDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        var activeId = this.service.GetActiveDocumentId(this.testWindowId);
        _ = activeId.Should().BeNull();
    }

    /// <summary>
    /// Test that a document has one tab: a second tab for it is refused, in its window or another.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task OpenDocumentAsync_ForADocumentWithATab_IsRefused()
    {
        // Arrange
        var metadata = new TestDocumentMetadata { Title = "Test Document" };
        _ = await this.service!.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Act
        var again = await this.service.OpenDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);
        var elsewhere = await this.service.OpenDocumentAsync(new WindowId(2), metadata).ConfigureAwait(false);
        var attached = await this.service.AttachDocumentAsync(new WindowId(2), metadata).ConfigureAwait(false);

        // Assert
        _ = again.Should().BeFalse();
        _ = elsewhere.Should().BeFalse();
        _ = attached.Should().BeFalse();
        _ = this.service.GetOpenDocuments(this.testWindowId).Should().ContainSingle();
        _ = this.service.GetOpenDocuments(new WindowId(2)).Should().BeEmpty();
    }

    /// <summary>
    /// Test that two documents may both have tabs that cannot be closed: the editors decide what each window shows.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task OpenDocumentAsync_WithSecondNonClosableDocument_KeepsBoth()
    {
        // Arrange
        var first = new TestDocumentMetadata { Title = "Doc 1", IsClosable = false };
        var second = new TestDocumentMetadata { Title = "Doc 2", IsClosable = false };
        _ = await this.service!.OpenDocumentAsync(this.testWindowId, first).ConfigureAwait(false);

        // Act
        var opened = await this.service.OpenDocumentAsync(this.testWindowId, second).ConfigureAwait(false);

        // Assert
        _ = opened.Should().BeTrue();
        _ = this.service.GetOpenDocuments(this.testWindowId).Should().HaveCount(2);
    }

    /// <summary>
    /// Test that attaching a document returns true.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AttachDocumentAsync_WithValidMetadata_ReturnsTrue()
    {
        // Arrange
        var metadata = new TestDocumentMetadata
        {
            Title = "Test Document",
        };

        // Act
        var result = await this.service!.AttachDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeTrue();
    }

    /// <summary>
    /// Test that attaching a document adds it to the window's collection.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AttachDocumentAsync_AddsDocumentToCollection()
    {
        // Arrange
        var docId = DocumentId.New();
        var metadata = new TestDocumentMetadata(docId)
        {
            Title = "Test Document",
        };

        // Act
        _ = await this.service!.AttachDocumentAsync(this.testWindowId, metadata).ConfigureAwait(false);

        // Assert
        var openDocs = this.service.GetOpenDocuments(this.testWindowId);
        _ = openDocs.Count.Should().Be(1);
        _ = openDocs[0].DocumentId.Should().Be(docId);
    }

    /// <summary>
    /// Test that selecting an existing document returns true.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SelectDocumentAsync_WithExistingDocument_ReturnsTrue()
    {
        // Arrange
        var metadata = new TestDocumentMetadata { Title = "Test Document" };
        _ = await this.service!.OpenDocumentAsync(this.testWindowId, metadata, shouldSelect: false).ConfigureAwait(false);
        var docId = metadata.DocumentId;

        // Act
        var result = await this.service.SelectDocumentAsync(this.testWindowId, docId).ConfigureAwait(false);

        // Assert
        _ = result.Should().BeTrue();
    }

    /// <summary>
    /// Test that selecting a document sets it as active.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SelectDocumentAsync_SetsDocumentAsActive()
    {
        // Arrange
        var metadata = new TestDocumentMetadata { Title = "Test Document" };
        _ = await this.service!.OpenDocumentAsync(this.testWindowId, metadata, shouldSelect: false).ConfigureAwait(false);
        var docId = metadata.DocumentId;

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
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SelectDocumentAsync_WithNonExistentDocument_ReturnsFalse()
    {
        // Arrange
        var nonExistentId = DocumentId.New();

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
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task GetOpenDocuments_ReturnsAllOpenDocuments()
    {
        // Arrange
        var metadata = new TestDocumentMetadata { Title = "Test Document" };
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
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task GetActiveDocumentId_ReturnsActiveDocumentId()
    {
        // Arrange
        var metadata = new TestDocumentMetadata { Title = "Test Document" };
        _ = await this.service!.OpenDocumentAsync(this.testWindowId, metadata, shouldSelect: true).ConfigureAwait(false);
        var docId = metadata.DocumentId;

        // Act
        var result = this.service.GetActiveDocumentId(this.testWindowId);

        // Assert
        _ = result.Should().NotBeNull();
        _ = result!.Value.Should().Be(docId);
    }
}
