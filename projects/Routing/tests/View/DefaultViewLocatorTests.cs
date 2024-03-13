// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.View;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

/// <summary>
/// Test cases for the <see cref="DefaultViewLocator" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public class DefaultViewLocatorTests
{
    private Mock<IServiceProvider>? serviceLocatorMock;
    private MyView? view;
    private DefaultViewLocator? viewLocator;

    /// <summary>
    /// Setup the view and the common services before each test.
    /// </summary>
    [TestInitialize]
    public void Initialize()
    {
        this.serviceLocatorMock = new Mock<IServiceProvider>();
        this.viewLocator = new DefaultViewLocator(this.serviceLocatorMock!.Object, null);

        this.view = new MyView(new MyViewModel());
    }

    /// <summary>
    /// Resolve the View using the runtime type of the ViewModel instance.
    /// </summary>
    [TestMethod]
    [TestCategory("Successful Resolution")]
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
    /// Resolve the View, explicitly providing the type of the ViewModel
    /// instance. The view should be registered as <see cref="IViewFor{T}" />
    /// where T is the same as the ViewModel type.
    /// </summary>
    [TestMethod]
    [TestCategory("Successful Resolution")]
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
    /// Resolve the View, using the ViewModel instance. The view was registered
    /// with its type and not as an <see cref="IViewFor{T}" />.
    /// </summary>
    [TestMethod]
    [TestCategory("Successful Resolution")]
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
    /// Resolve the View, using the ViewModel instance. The view was registered
    /// with a base type and not as an <see cref="IViewFor{T}" />. The
    /// ViewModel type is also derived from a base type that can be deducted
    /// using the same convention to deduct a View type from a ViewModel type.
    /// </summary>
    [TestMethod]
    [TestCategory("Successful Resolution")]
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
    /// Resolve the View, using the ViewModel instance and its runtime type.
    /// The view was registered with an interface type and not as an
    /// <see cref="IViewFor{T}" />.
    /// </summary>
    [TestMethod]
    [TestCategory("Successful Resolution")]
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
    /// Resolve the View, using the ViewModel instance and its base class type.
    /// The view was registered with a base interface type and not as an
    /// <see cref="IViewFor{T}" />.
    /// The view's base interface type follows the deduction rules or ViewModel to View
    /// name.
    /// </summary>
    [TestMethod]
    [TestCategory("Successful Resolution")]
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
    /// Resolve the View, using the ViewModel instance and its interface base type.
    /// The view was registered with a class type and not as an
    /// <see cref="IViewFor{T}" />.
    /// The view's base interface type follows the deduction rules or ViewModel to View
    /// name.
    /// </summary>
    [TestMethod]
    [TestCategory("Successful Resolution")]
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

#pragma warning disable SA1201 // Elements should appear in the correct order
    private interface IMyView;

    private interface IMyViewModel;

    private sealed class MyViewModel : BaseViewModel, IBaseViewModel, IMyViewModel;

    private sealed class MyView(MyViewModel viewModel) : BaseView(viewModel), IViewFor<MyViewModel>
    {
        public new event EventHandler<ViewModelChangedEventArgs<MyViewModel>>? ViewModelChanged;

        public new MyViewModel? ViewModel { get; set; } = viewModel;

        object? IViewFor.ViewModel { get; set; } = viewModel;
    }
#pragma warning restore SA1201 // Elements should appear in the correct order
}

#pragma warning disable SA1201 // Elements should appear in the correct order
internal interface IBaseViewModel;

internal interface IBaseView;

internal class BaseViewModel;

[ExcludeFromCodeCoverage]
internal class BaseView(IBaseViewModel viewModel) : IViewFor<IBaseViewModel>
{
#pragma warning disable CS0067 // Never used
    public event EventHandler<ViewModelChangedEventArgs<IBaseViewModel>>? ViewModelChanged;
#pragma warning restore CS0067 // Never used

    public IBaseViewModel? ViewModel { get; set; } = viewModel;

    object? IViewFor.ViewModel { get; set; } = viewModel;
}
#pragma warning restore SA1201 // Elements should appear in the correct order
