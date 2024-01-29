// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.View;

using System.Diagnostics;
using System.Reflection;

/// <summary>
/// Default implementation for <see cref="IViewLocator" />.It uses a series of
/// heuristics to attempt to resolve the view model's type into a corresponding
/// view.
/// </summary>
/// <remarks>
/// Given view model type <c>VM_T</c> with runtime type <c>VM_RT</c>, this
/// implementation will attempt to resolve the following views:
/// <list type="number">
/// <item>
/// <description>
/// Look for a service registered under the type <see cref="IViewFor{T}" />
/// where <c>T</c> is the runtime type of the view model (i.e. <c>VM_RT</c>).
/// </description>
/// </item>
/// <item>
/// <description>
/// Look for a service registered under the type <see cref="IViewFor{T}" />
/// where <c>T</c> is the explicit type of the view model (i.e. <c>VM_T</c>).
/// </description>
/// </item>
/// <item>
/// <description>
/// Look for a service registered under the type whose name is given to us by
/// passing <c>VM_RT</c> to <see cref="ViewModelToViewFunc" /> (which defaults
/// to changing "ViewModel" to "View").
/// </description>
/// </item>
/// <item>
/// <description>
/// Look for a service registered under the type whose name is given to us by
/// passing <c>VM_T</c> to <see cref="ViewModelToViewFunc" /> (which defaults
/// to changing "ViewModel" to "View").
/// </description>
/// </item>
/// <item>
/// <description>
/// If <c>VM_RT</c> is an interface, change its name to that of a class (i.e.
/// drop the leading "I"). If it's a class, change to an interface (i.e. add a
/// leading "I"). Try again step 3 with the new name.
/// </description>
/// </item>
/// <item>
/// <description>
/// If <c>VM_T</c> is an interface, change its name to that of a class (i.e.
/// drop the leading "I"). If it's a class, change to an interface (i.e. add a
/// leading "I"). Try again step 4 with the new name.
/// </description>
/// </item>
/// </list>
/// <para>
/// The view locator relies on the IoC dependency injector to obtain the view
/// for the view model. The <see cref="IServiceProvider" /> of the dependency
/// injector is itself injected in the constructor of the
/// <see cref="DefaultViewLocator" />
/// and used as a service locator.
/// </para>
/// <para>
/// If the <see cref="DefaultViewLocator" /> is not obtained via DI and is
/// rather created manually, then the <see cref="IServiceProvider" /> instance
/// must be provided to the constructor.
/// </para>
/// </remarks>
public class DefaultViewLocator : IViewLocator
{
    private readonly IServiceProvider serviceLocator;

    /// <summary>
    /// Initializes a new instance of the <see cref="DefaultViewLocator" />
    /// class using the default implementation for the function mapping view
    /// model's name to view name.
    /// </summary>
    /// <param name="serviceLocator">
    /// The DI <see cref="IServiceProvider" />.
    /// </param>
    public DefaultViewLocator(IServiceProvider serviceLocator)
    {
        this.serviceLocator = serviceLocator;
        this.ViewModelToViewFunc = vm => vm.Replace("ViewModel", "View");
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="DefaultViewLocator" />
    /// class.
    /// </summary>
    /// <param name="serviceLocator">
    /// The DI <see cref="IServiceProvider" />.
    /// </param>
    /// <param name="viewModelToViewFunc">
    /// The method which will convert a 'ViewModel' name into a 'View' name.
    /// </param>
    public DefaultViewLocator(IServiceProvider serviceLocator, Func<string, string> viewModelToViewFunc)
    {
        this.serviceLocator = serviceLocator;
        this.ViewModelToViewFunc = viewModelToViewFunc;
    }

    /// <summary>
    /// Gets the function that is used to convert a view model name to a
    /// potential view name.
    /// </summary>
    /// <remarks>
    /// <para>
    /// If unset, the default behavior is to change "ViewModel" to "View". If a
    /// different convention is followed, assign an appropriate function to
    /// this property.
    /// </para>
    /// <para>
    /// Note that the name returned by the function is a starting point for
    /// view resolution. Variants on the name will be resolved according to the
    /// rules set out by the <see cref="ResolveView{T}" /> method.
    /// </para>
    /// </remarks>
    /// <value>
    /// A function that is used to convert a view model name to a potential
    /// view name.
    /// </value>
    private Func<string, string> ViewModelToViewFunc { get; }

    /// <inheritdoc />
    public object? ResolveView(object viewModel)
    {
        var viewModelType = viewModel.GetType();

        var view = this.AttemptViewForResolutionFor(viewModelType);
        if (view is not null)
        {
            Debug.WriteLine(
                $"Resolved view for VM RT {viewModelType.FullName} using IViewFor<{viewModelType.FullName}>");
            return view;
        }

        view = this.AttemptViewResolutionFor(viewModelType);
        if (view is not null)
        {
            Debug.WriteLine($"Resolved view for {viewModelType.FullName} using View {view.GetType().FullName}");
            return view;
        }

        view = this.AttemptViewResolutionFor(ToggleViewModelType(viewModelType));
        if (view is not null)
        {
            Debug.WriteLine(
                $"Resolved view for {viewModelType.FullName} using View {view.GetType().FullName} and toggle");
            return view;
        }

        Debug.WriteLine($"Failed to resolve view for view model type '{viewModelType.FullName}'.");
        return null;
    }

    /// <inheritdoc />
    public IViewFor<T>? ResolveView<T>()
        where T : class
    {
        var view = this.AttemptViewForResolutionFor(typeof(T));
        if (view is not null)
        {
            Debug.WriteLine($"Resolved view for VM T {typeof(T).FullName} using IViewFor<{typeof(T).FullName}>");
            return (IViewFor<T>?)view;
        }

        view = this.AttemptViewResolutionFor(typeof(T));
        if (view is not null)
        {
            Debug.WriteLine($"Resolved view for {typeof(T).FullName} using View {view.GetType().FullName}");
            return (IViewFor<T>?)view;
        }

        view = this.AttemptViewResolutionFor(ToggleViewModelType(typeof(T)));
        if (view is not null)
        {
            Debug.WriteLine($"Resolved view for {typeof(T).FullName} using View {view.GetType().FullName} and toggle");
            return (IViewFor<T>?)view;
        }

        Debug.WriteLine($"Failed to resolve view for view model type '{typeof(T).FullName}'.");
        return null;
    }

    private static Type? ToggleViewModelType(Type viewModelType)
    {
        var viewModelTypeName = viewModelType.AssemblyQualifiedName;

        if (viewModelTypeName is null)
        {
            return null;
        }

        string toggledTypeName;
        if (viewModelType.GetTypeInfo().IsInterface)
        {
            if (!viewModelType.Name.StartsWith('I'))
            {
                return null;
            }

            var idxComma = viewModelTypeName.IndexOf(',', 0);
            var idxPlus = viewModelTypeName.LastIndexOf('+', idxComma - 1);
            if (idxPlus != -1)
            {
                toggledTypeName = string.Concat(
                    viewModelTypeName.AsSpan(0, idxPlus + 1),
                    viewModelTypeName.AsSpan(idxPlus + 2));
            }
            else
            {
                var idxPeriod = viewModelTypeName.LastIndexOf('.', idxComma - 1);
                toggledTypeName = string.Concat(
                    viewModelTypeName.AsSpan(0, idxPeriod + 1),
                    viewModelTypeName.AsSpan(idxPeriod + 2));
            }
        }
        else
        {
            var idxComma = viewModelTypeName.IndexOf(',', 0);
            var idxPlus = viewModelTypeName.LastIndexOf('+', idxComma - 1);
            if (idxPlus != -1)
            {
                toggledTypeName = viewModelTypeName.Insert(idxPlus + 1, "I");
            }
            else
            {
                var idxPeriod = viewModelTypeName.LastIndexOf('.', idxComma - 1);
                toggledTypeName = viewModelTypeName.Insert(idxPeriod + 1, "I");
            }
        }

        return Type.GetType(toggledTypeName, false);
    }

    private IViewFor? AttemptViewForResolutionFor(Type? viewModelType)
    {
        var proposedViewTypeName = typeof(IViewFor<>).MakeGenericType(viewModelType!).AssemblyQualifiedName;
        return proposedViewTypeName is null ? null : this.AttemptViewResolution(proposedViewTypeName);
    }

    private IViewFor? AttemptViewResolutionFor(Type? viewModelType)
    {
        var viewModelTypeName = viewModelType?.AssemblyQualifiedName;
        if (viewModelTypeName is null)
        {
            return null;
        }

        var proposedViewTypeName = this.ViewModelToViewFunc(viewModelTypeName);
        return this.AttemptViewResolution(proposedViewTypeName);
    }

    private IViewFor? AttemptViewResolution(string viewTypeName)
    {
        try
        {
            var viewType = Type.GetType(viewTypeName, false);
            if (viewType is null)
            {
                return null;
            }

            var service = this.serviceLocator.GetService(viewType);

            if (service is not IViewFor view)
            {
                return null;
            }

            Debug.WriteLine($"Resolved service type '{viewType.FullName}'");

            return view;
        }
        catch (Exception)
        {
            Debug.WriteLine($"Exception occurred while attempting to resolve type {viewTypeName} into a view.");
            throw;
        }
    }
}
