// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Moq;
using IContainer = DryIoc.IContainer;

namespace DroidNet.Mvvm.Tests;

/// <summary>Test cases for the <see cref="DefaultViewLocator" /> class.</summary>
[TestClass]
[TestCategory(nameof(DefaultViewLocatorTests))]
[ExcludeFromCodeCoverage]
public class DefaultViewLocatorTests
{
    private Mock<IContainer>? serviceLocatorMock;
    private MyView? view;
    private DefaultViewLocator? viewLocator;

    /// <summary>Set up the view and the common services before each test.</summary>
    [TestInitialize]
    public void Initialize()
    {
        this.serviceLocatorMock = new Mock<IContainer>();
        this.viewLocator = new DefaultViewLocator(this.serviceLocatorMock!.Object, loggerFactory: null);

        this.view = new MyView(new MyViewModel());
    }

    /// <summary>Resolve the View using the runtime type of the ViewModel instance.</summary>
    [TestMethod]
    public void ViewFromVmRuntimeType()
    {
        _ = this.serviceLocatorMock!.Setup(m => m.GetService(typeof(IViewFor<MyViewModel>))).Returns(this.view);

        var resolved = this.viewLocator!.ResolveView(new MyViewModel());

        _ = resolved.Should()
            .NotBeNull(
                """
                because we have registered an `IViewFor<MyViewModel>` and we
                are requesting the view for a model with runtime type `MyViewModel`.
                """);
    }

    /// <summary>
    /// Resolve the View, explicitly providing the type of the ViewModel instance. The view should be registered as
    /// <see cref="IViewFor{T}" /> where T is the same as the ViewModel type.
    /// </summary>
    [TestMethod]
    public void ViewFromVmType()
    {
        _ = this.serviceLocatorMock!.Setup(m => m.GetService(typeof(IViewFor<IBaseViewModel>))).Returns(this.view);

        var resolved = this.viewLocator!.ResolveView<IBaseViewModel>();

        _ = resolved.Should()
            .NotBeNull(
                """
                because we have registered an `IViewFor<IBaseViewModel>` and we
                are requesting the view for a model with explicit type `IBaseViewModel`.
                """);
    }

    /// <summary>
    /// Resolve the View, using the ViewModel instance. The view was registered with its type and not as a
    /// <see cref="IViewFor{T}" />.
    /// </summary>
    [TestMethod]
    public void ViewRegisteredWithViewTypeFromVmRuntimeType()
    {
        _ = this.serviceLocatorMock!.Setup(m => m.GetService(typeof(MyView))).Returns(this.view);

        var resolved = this.viewLocator!.ResolveView(new MyViewModel());

        _ = resolved.Should()
            .NotBeNull(
                """
                because we have registered a view with type `MyView` and we are
                requesting the view for a model with runtime type `MyViewModel`.
                """);
    }

    /// <summary>
    /// Resolve the View, using the ViewModel instance. The view was registered with a base type and not as a
    /// <see cref="IViewFor{T}" />. The ViewModel type is also derived from a base type that can be deducted using the same
    /// convention to deduct a View type from a ViewModel type.
    /// </summary>
    [TestMethod]
    public void ViewRegisteredWithBaseViewTypeFromVmBaseType()
    {
        _ = this.serviceLocatorMock!.Setup(m => m.GetService(typeof(IBaseView))).Returns(this.view);

        var resolved = this.viewLocator!.ResolveView<IBaseViewModel>();

        _ = resolved.Should()
            .NotBeNull(
                """
                because we have registered a view which implements the interface
                `IBaseView` and we are requesting the view for a model with
                explicit type `IBaseViewModel`.
                """);
    }

    /// <summary>
    /// Resolve the View, using the ViewModel instance and its runtime type. The view was registered with an interface type and
    /// not as a <see cref="IViewFor{T}" />.
    /// </summary>
    [TestMethod]
    public void ViewFromVmRuntimeTypeWithInterfaceToggle()
    {
        _ = this.serviceLocatorMock!.Setup(m => m.GetService(typeof(IMyView))).Returns(this.view);

        var resolved = this.viewLocator!.ResolveView(new MyViewModel());

        _ = resolved.Should()
            .NotBeNull(
                """
                because we have registered a view which implements the interface
                `IMyView` and we are requesting the view for a model with runtime
                type `MyViewModel`. The interface toggle will match the types.
                """);
    }

    /// <summary>
    /// Resolve the View, using the ViewModel instance and its base class type. The view was registered with a base interface type
    /// and not as a <see cref="IViewFor{T}" />. The view's base interface type follows the deduction rules or ViewModel to View
    /// name.
    /// </summary>
    [TestMethod]
    public void ViewFromVmClassTypeWithInterfaceToggle()
    {
        _ = this.serviceLocatorMock!.Setup(m => m.GetService(typeof(IBaseView))).Returns(this.view);

        var resolved = this.viewLocator!.ResolveView<IBaseViewModel>();

        _ = resolved.Should()
            .NotBeNull(
                """
                because we have registered a view which implements the base
                interface `IBaseView` and we are requesting the view for a model
                with explicit type `IBaseViewModel`. The interface toggle will
                match the types.
                """);
    }

    /// <summary>
    /// Resolve the View, using the ViewModel instance and its interface base type. The view was registered with a class type and
    /// not as a <see cref="IViewFor{T}" />. The view's base interface type follows the deduction rules or ViewModel to View
    /// name.
    /// </summary>
    [TestMethod]
    public void ViewFromVmBaseInterfaceTypeWithClassToggle()
    {
        _ = this.serviceLocatorMock!.Setup(m => m.GetService(typeof(BaseView))).Returns(this.view);

        var resolved = this.viewLocator!.ResolveView<IBaseViewModel>();

        _ = resolved.Should()
            .NotBeNull(
                """
                because we have registered a view with type `BaseView` and we
                are requesting the view for a model with explicit interface
                `IBaseViewModel`. The class toggle will match the types.
                """);
    }

    /// <summary>
    /// Test that inheritance scenarios work when both base and derived ViewModels are explicitly registered
    /// with the same View instance. MyView only implements IViewFor&lt;MyViewModel&gt; but can be registered
    /// to handle MyExtendedViewModel as well. This demonstrates Option 1 for handling ViewModel inheritance.
    /// </summary>
    [TestMethod]
    public void ViewFromExtendedViewModelWithExplicitRegistration()
    {
        // Register the SAME MyView instance for both the base and extended ViewModel types
        // Note: MyView only implements IViewFor<MyViewModel>, but DI allows us to register
        // it for IViewFor<MyExtendedViewModel> as well since MyExtendedViewModel : MyViewModel
        _ = this.serviceLocatorMock!.Setup(m => m.GetService(typeof(IViewFor<MyViewModel>))).Returns(this.view);
        _ = this.serviceLocatorMock!.Setup(m => m.GetService(typeof(IViewFor<MyExtendedViewModel>))).Returns(this.view);

        // Test that the extended ViewModel resolves to the same MyView instance
        var resolved = this.viewLocator!.ResolveView(new MyExtendedViewModel());

        _ = resolved.Should()
            .NotBeNull(
                """
                because we have explicitly registered the same MyView instance for both
                `IViewFor<MyViewModel>` and `IViewFor<MyExtendedViewModel>`. Even though
                MyView only implements IViewFor<MyViewModel>, the DI registration allows
                the same view instance to handle derived ViewModels.
                """);

        _ = resolved.Should().BeOfType<MyView>();
        _ = resolved.Should().BeSameAs(this.view, "the same view instance should be returned for both base and derived ViewModels");
    }

    /// <summary>
    /// Test that demonstrates the default behavior when an extended ViewModel is NOT explicitly registered.
    /// This shows why Option 1 (explicit registration) is necessary for inheritance scenarios.
    /// </summary>
    [TestMethod]
    public void ExtendedViewModelWithoutExplicitRegistrationReturnsNull()
    {
        // Only register the base ViewModel, not the extended one
        _ = this.serviceLocatorMock!.Setup(m => m.GetService(typeof(IViewFor<MyViewModel>))).Returns(this.view);

        // Try to resolve view for extended ViewModel - should return null
        var resolved = this.viewLocator!.ResolveView(new MyExtendedViewModel());

        _ = resolved.Should()
            .BeNull(
                """
                because the view locator uses exact type matching and there is no
                explicit registration for `IViewFor<MyExtendedViewModel>`. Even though
                MyExtendedViewModel inherits from MyViewModel, the locator does not
                walk the inheritance hierarchy to find base type registrations.
                """);
    }

#pragma warning disable SA1201 // Elements should appear in the correct order
    private interface IMyView;

    private interface IMyViewModel;

    private class MyViewModel : BaseViewModel, IBaseViewModel, IMyViewModel;

    private sealed class MyView(MyViewModel viewModel) : BaseView(viewModel), IViewFor<MyViewModel>
    {
        public new MyViewModel? ViewModel { get; set; } = viewModel;

        object? IViewFor.ViewModel { get; set; } = viewModel;

        event EventHandler<ViewModelChangedEventArgs<MyViewModel>>? IViewFor<MyViewModel>.ViewModelChanged
        {
            add { } // empty accessors so the compiler stops complaining about the event unused
            remove { }
        }
    }

    private sealed class MyExtendedViewModel : MyViewModel;
#pragma warning restore SA1201 // Elements should appear in the correct order
}

#pragma warning disable SA1201 // Elements should appear in the correct order
internal interface IBaseViewModel;

internal interface IBaseView;

[ExcludeFromCodeCoverage]
[SuppressMessage("StyleCop.CSharp.MaintainabilityRules", "SA1402:File may only contain a single type", Justification = "class only used in this test suite")]
internal class BaseViewModel;

[ExcludeFromCodeCoverage]
[SuppressMessage("StyleCop.CSharp.MaintainabilityRules", "SA1402:File may only contain a single type", Justification = "class only used in this test suite")]
internal class BaseView(IBaseViewModel viewModel) : IViewFor<IBaseViewModel>
{
    public IBaseViewModel? ViewModel { get; set; } = viewModel;

    object? IViewFor.ViewModel { get; set; } = viewModel;

    public event EventHandler<ViewModelChangedEventArgs<IBaseViewModel>>? ViewModelChanged
    {
        add { } // empty accessors so the compiler stops complaining about the event unused
        remove { }
    }
}
#pragma warning restore SA1201 // Elements should appear in the correct order
