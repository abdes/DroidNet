// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Aura.Drag;
using FluentAssertions;

namespace DroidNet.Aura.Tests.Drag;

/// <summary>
/// Unit tests for <see cref="DragSessionToken"/> struct.
/// These are pure unit tests that don't require UI thread.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DragSessionTokenTests")]
public class DragSessionTokenTests
{
    /// <summary>
    /// Verifies that tokens with the same ID are considered equal.
    /// </summary>
    [TestMethod]
    public void Tokens_WithSameId_AreEqual()
    {
        // Arrange
        var id = Guid.NewGuid();
        var token1 = new DragSessionToken { Id = id };
        var token2 = new DragSessionToken { Id = id };

        // Act & Assert
        _ = token1.Should().Be(token2, "Tokens with same ID should be equal");
        _ = (token1 == token2).Should().BeTrue("Equality operator should return true");
        _ = (token1 != token2).Should().BeFalse("Inequality operator should return false");
        _ = token1.Equals(token2).Should().BeTrue("Equals method should return true");
        _ = token1.GetHashCode().Should().Be(token2.GetHashCode(), "Hash codes should match for equal tokens");
    }

    /// <summary>
    /// Verifies that tokens with different IDs are not equal.
    /// </summary>
    [TestMethod]
    public void Tokens_WithDifferentIds_AreNotEqual()
    {
        // Arrange
        var token1 = new DragSessionToken { Id = Guid.NewGuid() };
        var token2 = new DragSessionToken { Id = Guid.NewGuid() };

        // Act & Assert
        _ = token1.Should().NotBe(token2, "Tokens with different IDs should not be equal");
        _ = (token1 == token2).Should().BeFalse("Equality operator should return false");
        _ = (token1 != token2).Should().BeTrue("Inequality operator should return true");
        _ = token1.Equals(token2).Should().BeFalse("Equals method should return false");
    }

    /// <summary>
    /// Verifies that default tokens are equal.
    /// </summary>
    [TestMethod]
    public void DefaultTokens_AreEqual()
    {
        // Arrange
        var token1 = default(DragSessionToken);
        var token2 = default(DragSessionToken);

        // Act & Assert
        _ = token1.Should().Be(token2, "Default tokens should be equal");
        _ = (token1 == token2).Should().BeTrue("Default tokens should be equal via operator");
    }

    /// <summary>
    /// Verifies that token equality works with null comparisons.
    /// </summary>
    [TestMethod]
    public void Token_Equals_HandlesNull()
    {
        // Arrange
        var token = new DragSessionToken { Id = Guid.NewGuid() };

        // Act & Assert
        _ = token.Equals(null).Should().BeFalse("Token should not equal null");
    }

    /// <summary>
    /// Verifies that token equality works with wrong type comparisons.
    /// </summary>
    [TestMethod]
    public void Token_Equals_HandlesWrongType()
    {
        // Arrange
        var token = new DragSessionToken { Id = Guid.NewGuid() };
        const string wrongType = "not a token";

        // Act & Assert
        _ = token.Equals(wrongType).Should().BeFalse("Token should not equal different type");
    }
}
