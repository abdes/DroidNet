// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Samples.NetClassLibrary.Tests;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Samples.NetClassLibrary;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

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
