// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Mvvm.Converters;

using System.Diagnostics;
using DroidNet.Mvvm;
using Microsoft.UI.Xaml.Data;

/// <summary>Provides conversion from a ViewModel to the corresponding View using the <see cref="IViewLocator" />.</summary>
/// <param name="viewLocator">The view locator used for resolution.</param>
/// <remarks>
/// In a ViewModel first MVVM approach, it is often the case that the application visual state is represented by a hierarchy of
/// ViewModel objects. When a View needs to present the content for a specific ViewModel as a child content, it is necessary to
/// transform that ViewModel into the corresponding View. This is straightforward from the code behind, using the view locator,
/// but requires a converter in XAML.
/// <para>
/// This converter needs to be made available as a StaticResource, and it is recommended to have this done once at the application
/// level. Things can get a bit complicated when a Dependency Injector is also being used because XAML and DI don't work well
/// together. The recommended approach is to add the resource in the code behind, for example in the <c>App.xaml.cs</c> file.
/// </para>
/// <code lang="cs"><![CDATA[
///     private const string VmToViewConverterResourceKey = "VmToViewConverter";
///     private readonly IValueConverter vmToViewConverter;
///
///     // We can have multiple converters, for different scenarios, differentiated by the `key` used to register them as
///     // services with the DI.
///     public App([FromKeyedServices("Default")] IValueConverter vmToViewConverter)
///     {
///         this.vmToViewConverter = vmToViewConverter;
///         this.InitializeComponent();
///     }
///
///     protected override void OnLaunched(LaunchActivatedEventArgs args)
///     {
///         Current.Resources[VmToViewConverterResourceKey] = this.vmToViewConverter;
///         ...
///     }
/// ]]></code>
/// </remarks>
/// <example>
/// <code lang="cs"><![CDATA[
///     <ContentPresenter Content="{x:Bind ViewModel.Workspace, Converter={StaticResource VmToViewConverter}}" />
/// ]]></code>
/// </example>
public class ViewModelToView(IViewLocator viewLocator) : IValueConverter
{
    /// <inheritdoc />
    public object? Convert(object? value, Type targetType, object? parameter, string? language)
    {
        if (value is null)
        {
            return null;
        }

        var view = viewLocator.ResolveView(value);
        if (view == null)
        {
            return view;
        }

        Debug.Assert(
            view is IViewFor,
            $"a resolved view object must implement `{nameof(IViewFor)}<T>` where `T` is the view model");

        // It's extremely important that the ViewModel property of the view is set here so that we have a completely transparent
        // management of the ViewModel property.
        ((IViewFor)view).ViewModel = value;

        return view;
    }

    /// <inheritdoc />
    public object ConvertBack(object? value, Type? targetType, object? parameter, string? language)
        => throw new InvalidOperationException();
}
