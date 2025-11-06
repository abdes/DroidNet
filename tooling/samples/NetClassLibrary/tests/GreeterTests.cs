// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;

namespace DroidNet.Samples.NetClassLibrary.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public class GreeterTests
{
    [TestMethod]
    public void Greeting_ContainsHello()
    {
        // Arrange
        var sut = new Greeter("Hello World!");

        // Act
        var greeting = sut.Greeting;

        // Assert
        _ = greeting.Should().Contain("Hello World!", "it's a Hello World library 😀");
    }
}
