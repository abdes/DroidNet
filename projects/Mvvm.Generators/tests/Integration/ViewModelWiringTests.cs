// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Mvvm.Generators.Tests.Demo;
using DroidNet.TestHelpers;
using DryIoc;
using AwesomeAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting.AppContainer;

namespace DroidNet.Mvvm.Generators.Tests;

/// <summary>
/// Integration tests for the View to ViewModel wiring source generator.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("ViewModel to View Wiring")]
[TestCategory("UITest")]
public class ViewModelWiringTests
{
    [UITestMethod]
    [DisplayName("Generated extensions do wire the ViewModel properly and export the ViewModel property")]
    public void GenerateViewExtensionsCorrectly()
    {
        var view = CommonTestEnv.TestContainer.Resolve<IViewFor<DemoViewModel>>();
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
    }

    [UITestMethod]
    [DisplayName("Generated extensions trigger the ViewModelChanged event when the ViewModel property is changed")]
    public void TriggerEventWhenViewModelIsChanged()
    {
        var view = CommonTestEnv.TestContainer.Resolve<IViewFor<DemoViewModel>>();
        _ = view.Should().NotBeNull();

        var eventTriggered = false;
        view.ViewModelChanged += (_, _) => eventTriggered = true;
        view.ViewModel = new DemoViewModel();

        _ = eventTriggered.Should()
            .BeTrue(
                $"""
                 the generated dependency property for the ViewModel property should
                 trigger the {nameof(DemoView.ViewModelChanged)} when the property is changed.
                 """);
    }
}
