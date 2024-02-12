// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Converters;

using System.Diagnostics;
using DroidNet.Routing.View;
using Microsoft.UI.Xaml.Data;

/// <summary>
/// Provides conversion from a ViewModel to the corresponding View using the
/// given <see cref="IViewLocator" />.
/// </summary>
/// <param name="viewLocator">The view locator used for resolution.</param>
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

        // The ViewModel property of the view is set as soon as it is loaded as
        // content inside an outlet.
        //
        // It's extremely important that the ViewModel property of the view is
        // set here so that we have a completely transparent management of the
        // ViewModel property.
        ((IViewFor)view).ViewModel = value;

        return view;
    }

    /// <inheritdoc />
    public object ConvertBack(object? value, Type? targetType, object? parameter, string? language)
        => throw new NotImplementedException();
}
