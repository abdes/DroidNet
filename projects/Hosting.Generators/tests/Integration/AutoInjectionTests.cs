// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Generators;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Hosting.Generators.Demo;
using FluentAssertions;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Integration tests for the View to ViewModel wiring source generator.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public class AutoInjectionTests
{
    /// <summary>
    /// Validate that the generated extensions do wire the ViewModel properly
    /// and export the ViewModel property.
    /// </summary>
    [TestMethod]
    [TestCategory("UITest")]
    public void AutoInjectionTest()
    {
        var sp = new ServiceCollection().UseAutoInject().BuildServiceProvider();

        _ = sp.GetService<SomeImplementation>().Should().NotBeNull();

        _ = sp.GetKeyedService<IMyInterface>("mine")
            .Should()
            .NotBeNull()
            .And.BeOfType(typeof(MyInterfaceImplementation));

        var nonDefaultConstructor = sp.GetService<NonDefaultConstructor>();
        _ = nonDefaultConstructor.Should().NotBeNull();
        _ = nonDefaultConstructor!.Injected.Should().NotBeNull();
    }
}
