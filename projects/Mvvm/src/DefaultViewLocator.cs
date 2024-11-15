// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Mvvm;

using System.Diagnostics;
using System.Reflection;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

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
/// <remarks>
/// Initializes a new instance of the <see cref="DefaultViewLocator" />
/// class using the default implementation for the function mapping view
/// model's name to view name.
/// </remarks>
/// <param name="container">
/// The DryIoc <see cref="IContainer" /> used to locate views and view models registered as services.
/// </param>
/// <param name="loggerFactory">
/// We inject a <see cref="ILoggerFactory" /> to be able to silently use a
/// <see cref="NullLogger" /> if we fail to obtain a <see cref="ILogger" />
/// from the Dependency Injector.
/// </param>
/// <param name="viewModelToViewFunc">
/// The method which will convert a 'ViewModel' name into a 'View' name.
/// </param>
public partial class DefaultViewLocator(
    IContainer container,
    ILoggerFactory? loggerFactory,
    Func<string, string>? viewModelToViewFunc = null) : IViewLocator
{
    private readonly ILogger logger = loggerFactory?.CreateLogger(typeof(DefaultViewLocator).Namespace!) ??
                                      NullLoggerFactory.Instance.CreateLogger(typeof(DefaultViewLocator).Namespace!);

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
        = viewModelToViewFunc ?? (vm => vm.Replace("ViewModel", "View", StringComparison.OrdinalIgnoreCase));

    /// <inheritdoc />
    public object? ResolveView(object viewModel)
    {
        var viewModelType = viewModel.GetType();

        var view = this.AttemptViewForResolutionFor(viewModelType);
        if (view is not null)
        {
            LogResolutionSuccess($"using IViewFor<{viewModelType.FullName}>");
            return view;
        }

        view = this.AttemptViewResolutionFor(viewModelType);
        if (view is not null)
        {
            LogResolutionSuccess("using the view type");
            return view;
        }

        view = this.AttemptViewResolutionFor(ToggleViewModelType(viewModelType));
        if (view is not null)
        {
            LogResolutionSuccess("using view type with toggle");
            return view;
        }

        LogViewResolutionFailed(this.logger, viewModelType);
        return null;

        void LogResolutionSuccess(string how)
        {
            Debug.Assert(view != null, nameof(view) + " != null");
            LogViewResolved(this.logger, viewModelType, view.GetType(), how);
        }
    }

    /// <inheritdoc />
    public IViewFor<T>? ResolveView<T>()
        where T : class
    {
        var view = this.AttemptViewForResolutionFor(typeof(T));
        if (view is not null)
        {
            LogResolutionSuccess($"using IViewFor<{typeof(T)}>");
            return (IViewFor<T>?)view;
        }

        view = this.AttemptViewResolutionFor(typeof(T));
        if (view is not null)
        {
            LogResolutionSuccess("using the view type");
            return (IViewFor<T>?)view;
        }

        view = this.AttemptViewResolutionFor(ToggleViewModelType(typeof(T)));
        if (view is not null)
        {
            LogResolutionSuccess("using the view type with toggle");
            return (IViewFor<T>?)view;
        }

        LogViewResolutionFailed(this.logger, typeof(T));
        return null;

        void LogResolutionSuccess(string how)
        {
            Debug.Assert(view != null, nameof(view) + " != null");
            LogViewResolved(this.logger, typeof(T), view.GetType(), how);
        }
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

        return Type.GetType(toggledTypeName, throwOnError: false);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Resolved ViewModel with type {ViewModelType} to {ViewType}, {How}")]
    private static partial void LogViewResolved(ILogger logger, Type viewModelType, Type viewType, string how);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Failed to get a View for the ViewModel with type {ViewModelType}")]
    private static partial void LogViewResolutionFailed(ILogger logger, Type viewModelType);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Could not get a View with type {ViewType} from the Dependency Injector")]
    private static partial void LogViewResolutionAttemptFailed(ILogger logger, string viewType, Exception exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "View with type {ViewType} does not implement IViewFor")]
    private static partial void LogViewIsNotViewFor(ILogger logger, Type viewType);

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

    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "we want to catch all, because we use diagnostics to report errors")]
    private IViewFor? AttemptViewResolution(string viewTypeName)
    {
        try
        {
            var viewType = Type.GetType(viewTypeName, throwOnError: false);
            if (viewType is null)
            {
                return null;
            }

            var service = container.GetService(viewType);
            if (service is IViewFor view)
            {
                return view;
            }

            if (service is not null)
            {
                LogViewIsNotViewFor(this.logger, viewType);
            }

            return null;
        }
        catch (Exception exception)
        {
            LogViewResolutionAttemptFailed(this.logger, viewTypeName, exception);
            return null;
        }
    }
}
