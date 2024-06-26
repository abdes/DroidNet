// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;

[TestClass]
[TestCategory(nameof(Length))]
[ExcludeFromCodeCoverage]
public class LengthTests
{
    [TestMethod]
    public void Create_WithInvalidString_Throws()
    {
        // Arrange
        const string value = "xyz";

        // Act
        var act = () => new Width(value);

        // Assert
        _ = act.Should().Throw<ArgumentException>();
    }
}
