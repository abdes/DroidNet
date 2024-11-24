// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.Windows.ApplicationModel.Resources;
using Moq;

namespace DroidNet.Resources.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(ResourceExtensions))]
public class ResourceExtensionsTests
{
    private readonly Mock<IResourceMap> mockResourceMap = new();

    public ResourceExtensionsTests()
    {
        _ = this.mockResourceMap.Setup(m => m.TryGetValue("Hello"))
            .Returns(new ResourceCandidate(ResourceCandidateKind.String, "Hola"));
        _ = this.mockResourceMap.Setup(m => m.TryGetValue("Goodbye"))
            .Returns(new ResourceCandidate(ResourceCandidateKind.String, "Adiós"));
        _ = this.mockResourceMap.Setup(m => m.TryGetValue(It.IsNotIn("Hello", "Goodbye")))
            .Returns((ResourceCandidate?)null);
        _ = this.mockResourceMap.Setup(m => m.GetSubtree(It.IsAny<string>()))
            .Returns(this.mockResourceMap.Object);
    }

    [TestMethod]
    public void GetLocalized_ShouldReturnLocalizedString_WhenStringExists()
    {
        // Arrange
        const string originalString = "Hello";

        // Act
        var localizedString = originalString.GetLocalized(this.mockResourceMap.Object);

        // Assert
        _ = localizedString.Should().Be("Hola");
    }

    [TestMethod]
    public void GetLocalized_ShouldReturnOriginalString_WhenStringDoesNotExist()
    {
        // Arrange
        const string originalString = "Welcome";

        // Act
        var localizedString = originalString.GetLocalized();

        // Assert
        _ = localizedString.Should().Be(originalString);
    }

    [TestMethod]
    public void GetLocalizedMine_ShouldReturnLocalizedString_WhenStringExists()
    {
        // Arrange
        const string originalString = "Goodbye";

        // Act
        var localizedString = originalString.GetLocalizedMine(this.mockResourceMap.Object);

        // Assert
        _ = localizedString.Should().Be("Adiós");
    }

    [TestMethod]
    public void GetLocalizedMine_ShouldReturnOriginalString_WhenStringDoesNotExist()
    {
        // Arrange
        const string originalString = "Welcome";

        // Act
        var localizedString = originalString.GetLocalizedMine();

        // Assert
        _ = localizedString.Should().Be(originalString);
    }

    [TestMethod]
    public void GetLocalizedMine_ShouldReturnOriginalString_WhenExceptionOccurs()
    {
        // Arrange
        const string originalString = "Hello";
        _ = this.mockResourceMap.Setup(m => m.TryGetValue(It.IsAny<string>())).Throws<Exception>();

        // Act
        var localizedString = originalString.GetLocalizedMine(this.mockResourceMap.Object);

        // Assert
        _ = localizedString.Should().Be(originalString);
    }

    [TestMethod]
    public void GetLocalizedMine_ShouldReturnOriginalString_WhenSubMapIsNull()
    {
        // Arrange
        const string originalString = "Hello";
        _ = this.mockResourceMap.Setup(m => m.GetSubtree(It.IsAny<string>())).Returns((IResourceMap?)null);

        // Act
        var localizedString = originalString.GetLocalizedMine(this.mockResourceMap.Object);

        // Assert
        _ = localizedString.Should().Be(originalString);
    }
}
