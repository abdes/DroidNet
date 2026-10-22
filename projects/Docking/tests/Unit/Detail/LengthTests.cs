// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Docking.Detail;

namespace DroidNet.Docking.Tests.Detail;

[TestClass]
[TestCategory(nameof(Length))]
[ExcludeFromCodeCoverage]
public class LengthTests
{
    [TestMethod]
    public void CreateWithInvalidStringThrows()
    {
        // Arrange
        const string value = "xyz";

        // Act
        var act = () => new Width(value);

        // Assert
        _ = act.Should().Throw<ArgumentException>();
    }
}
