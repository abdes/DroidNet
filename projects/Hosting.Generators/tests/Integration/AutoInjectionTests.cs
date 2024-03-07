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

        _ = sp.GetService<NonDefaultConstructor>().Should().NotBeNull();

        /*
        sc.AddSingleton<ITargetInterface>(sp => ActivatorUtilities.CreateInstance<MyImplementation>(sp));
        sc.AddSingleton(sp => ActivatorUtilities.CreateInstance<MyBadImplementation>(sp));
        sc.AddSingleton(sp => ActivatorUtilities.CreateInstance<RedundantImplementationType>(sp));
        sc.AddSingleton(sp => ActivatorUtilities.CreateInstance<NonDefaultConstructor>(sp));
        sc.AddKeyedSingleton(
            "key",
            (sp, _) => ActivatorUtilities.CreateInstance<NonDefaultConstructor>(sp));
        */

        /*
        var view = Ioc.Default.GetService<IViewFor<DemoViewModel>>();
        _ = view.Should()
            .NotBeNull(
                $"""
                 the view class {nameof(DemoView)} should have been registered
                 in the Ioc container as an `IViewFor<{nameof(DemoViewModel)}>`
                 """);

        var vm = view!.ViewModel;
        _ = vm.Should()
            .BeNull(
                """
                the initial value for the generated dependency property should
                not be set
                """);
        */
    }
}
