// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace DroidNet.Samples.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public class SampleTest
{
    [TestMethod]
    [DataRow(1, 1, 2)]
    [DataRow(5, 24, 29)]
    public void AddTwoNumbers_Works(int first, int second, int expected)
    {
        // Act
        var sum = first + second;

        // Assert
        _ = sum.Should().Be(expected);
    }
}
