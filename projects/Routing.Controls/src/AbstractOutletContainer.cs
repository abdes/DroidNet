// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using CommunityToolkit.Mvvm.ComponentModel;

/// <summary>
/// An abstract base classes for a typical outlet container.
/// <remarks>
/// The outlets are simply specified in a dictionary, where the key is the
/// outlet name and the value is an outlet descriptor tuple containing the
/// property name corresponding to the outlet and the viewmodel for the outlet
/// when it is activated.
/// <para>
/// When implementing an outlet container, simply populate the
/// <see cref="Outlets">Outlets</see> dictionary. For each outlet, ensure that
/// the outlet name is unique (preferably across the application, but at least
/// within the context of this outlet container), and that for each outlet,
/// there is a property that can be used to bind to the outlet view model.
/// </para>
/// </remarks>
/// </summary>
public abstract class AbstractOutletContainer : ObservableObject, IOutletContainer, IDisposable
{
    protected Dictionary<OutletName, (string propertyName, object? viewModel)> Outlets { get; }
        = new(OutletName.EqualityComparer.IgnoreCase);

    /// <inheritdoc />
    public void LoadContent(object viewModel, OutletName? outletName = null)
    {
        outletName ??= OutletName.Primary;
        if (!this.Outlets.TryGetValue(outletName, out var outlet))
        {
            throw new ArgumentException($"unknown outlet name {outletName}", nameof(outletName));
        }

        /*
         Proceed with the update only if we are not reusing the same view model.
         That way, we avoid unnecessary change notifications.
        */

        if (outlet.viewModel == viewModel)
        {
            return;
        }

        // Dispose of the existing view model if it is IDisposable
        if (outlet.viewModel is IDisposable resource)
        {
            resource.Dispose();
        }

        this.Outlets[outletName] = (outlet.propertyName, viewModel);
        this.OnPropertyChanged(outlet.propertyName);
    }

    /// <inheritdoc />
    /// <remarks>
    /// The outlets in the outlet container may have been activated, and as a
    /// result, they would have a view model which may be disposable. When the
    /// container is disposed of, we should dispose of all the activated outlet
    /// view models as well.
    /// </remarks>
    public void Dispose()
    {
        foreach (var entry in this.Outlets)
        {
            if (entry.Value.viewModel is IDisposable resource)
            {
                resource.Dispose();
            }
        }

        GC.SuppressFinalize(this);
    }
}
