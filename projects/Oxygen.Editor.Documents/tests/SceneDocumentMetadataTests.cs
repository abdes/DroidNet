// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>
/// Unit tests for <see cref="SceneDocumentMetadata"/>.
/// </summary>
[TestClass]
[TestCategory("Unit")]
[ExcludeFromCodeCoverage]
public class SceneDocumentMetadataTests
{
    /// <summary>
    /// Test that DocumentType returns "Scene".
    /// </summary>
    [TestMethod]
    public void DocumentType_ReturnsScene()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata();

        // Act
        var result = metadata.DocumentType;

        // Assert
        _ = result.Should().Be("Scene");
    }

    /// <summary>
    /// Test that Layout defaults to OnePane.
    /// </summary>
    [TestMethod]
    public void Layout_DefaultsToOnePane()
    {
        // Arrange & Act
        var metadata = new SceneDocumentMetadata();

        // Assert
        _ = metadata.Layout.Should().Be(SceneViewLayout.OnePane);
    }

    /// <summary>
    /// Test that Layout can be set to different values.
    /// </summary>
    [TestMethod]
    public void Layout_CanBeSet()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata
        {
            Layout = SceneViewLayout.FourQuad,
        };

        // Assert
        _ = metadata.Layout.Should().Be(SceneViewLayout.FourQuad);
    }

    /// <summary>
    /// Test that all IDocumentMetadata properties can be set.
    /// </summary>
    [TestMethod]
    public void IDocumentMetadataProperties_CanBeSet()
    {
        // Arrange
        var docId = Guid.NewGuid();
        var iconUri = new Uri("ms-appx:///Assets/icon.png");

        // Act
        var metadata = new SceneDocumentMetadata(docId)
        {
            Title = "Test Scene",
            IconUri = iconUri,
            IsDirty = true,
            IsPinnedHint = true,
            IsClosable = false,
        };

        // Assert
        _ = metadata.DocumentId.Should().Be(docId);
        _ = metadata.Title.Should().Be("Test Scene");
        _ = metadata.IconUri.Should().Be(iconUri);
        _ = metadata.IsDirty.Should().BeTrue();
        _ = metadata.IsPinnedHint.Should().BeTrue();
        _ = metadata.IsClosable.Should().BeFalse();
    }

    /// <summary>
    /// Test that Title defaults to empty string.
    /// </summary>
    [TestMethod]
    public void Title_DefaultsToEmptyString()
    {
        // Arrange & Act
        var metadata = new SceneDocumentMetadata();

        // Assert
        _ = metadata.Title.Should().Be(string.Empty);
    }

    /// <summary>
    /// Test that IconUri defaults to null.
    /// </summary>
    [TestMethod]
    public void IconUri_DefaultsToNull()
    {
        // Arrange & Act
        var metadata = new SceneDocumentMetadata();

        // Assert
        _ = metadata.IconUri.Should().BeNull();
    }

    /// <summary>
    /// Test that IsDirty defaults to false.
    /// </summary>
    [TestMethod]
    public void IsDirty_DefaultsToFalse()
    {
        // Arrange & Act
        var metadata = new SceneDocumentMetadata();

        // Assert
        _ = metadata.IsDirty.Should().BeFalse();
    }

    /// <summary>
    /// Test that IsPinnedHint defaults to false.
    /// </summary>
    [TestMethod]
    public void IsPinnedHint_DefaultsToFalse()
    {
        // Arrange & Act
        var metadata = new SceneDocumentMetadata();

        // Assert
        _ = metadata.IsPinnedHint.Should().BeFalse();
    }

    /// <summary>
    /// Test that IsClosable defaults to true.
    /// </summary>
    [TestMethod]
    public void IsClosable_DefaultsToTrue()
    {
        // Arrange & Act
        var metadata = new SceneDocumentMetadata();

        // Assert
        _ = metadata.IsClosable.Should().BeTrue();
    }

    /// <summary>
    /// Test that metadata can be created with object initializer.
    /// </summary>
    [TestMethod]
    public void Metadata_CanBeCreatedWithObjectInitializer()
    {
        // Arrange & Act
        var metadata = new SceneDocumentMetadata
        {
            Title = "My Scene",
            Layout = SceneViewLayout.TwoMainLeft,
            IsDirty = true,
        };

        // Assert
        _ = metadata.Title.Should().Be("My Scene");
        _ = metadata.Layout.Should().Be(SceneViewLayout.TwoMainLeft);
        _ = metadata.IsDirty.Should().BeTrue();
    }

    /// <summary>
    /// Test that metadata properties can be modified after creation.
    /// </summary>
    [TestMethod]
    public void Metadata_PropertiesCanBeModified()
    {
        // Arrange
        var metadata = new SceneDocumentMetadata { Title = "Original" };

        // Act
        metadata.Title = "Modified";
        metadata.IsDirty = true;
        metadata.Layout = SceneViewLayout.ThreeMainTop;

        // Assert
        _ = metadata.Title.Should().Be("Modified");
        _ = metadata.IsDirty.Should().BeTrue();
        _ = metadata.Layout.Should().Be(SceneViewLayout.ThreeMainTop);
    }
}
