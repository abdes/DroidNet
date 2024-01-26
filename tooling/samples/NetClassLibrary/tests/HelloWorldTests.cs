// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Samples.NetClassLibrary;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Samples.Samples.NetClassLibrary;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
public class HelloWorldTests
{
    [TestMethod]
    public void Greeting_ContainsHello()
    {
        // Arrange
        var sut = new HelloWorld();

        // Act
        var greeting = sut.Greeting;

        // Assert
        _ = greeting.Should().Contain("Hello", "it's a greeting 😀");
    }
}
