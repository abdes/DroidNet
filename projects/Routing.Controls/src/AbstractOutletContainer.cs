// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using CommunityToolkit.Mvvm.ComponentModel;

public abstract class AbstractOutletContainer : ObservableObject, IOutletContainer
{
    protected abstract Dictionary<string, object?> Outlets { get; }

    /// <inheritdoc />
    public void LoadContent(object viewModel, OutletName? outletName = null)
    {
        outletName ??= OutletName.Primary;
        if (!this.Outlets.TryGetValue(outletName, out var existing))
        {
            throw new ArgumentException($"unknown outlet name {outletName}", nameof(outletName));
        }

        if (existing is IDisposable resource)
        {
            resource.Dispose();
        }

        this.Outlets[outletName] = viewModel;
        this.OnPropertyChanged((string?)this.GetPropertyNameForOutlet(outletName));
    }

    public object? GetViewModelForOutlet(string? outletName)
    {
        outletName ??= OutletName.Primary;
        return this.Outlets.GetValueOrDefault(outletName);
    }

    public string GetPropertyNameForOutlet(string? outletName)
    {
        outletName ??= OutletName.Primary;
        if (this.Outlets.ContainsKey(outletName))
        {
            return $"{outletName}ViewModel";
        }

        throw new ArgumentException($"unknown outlet name {outletName}", nameof(outletName));
    }
}
