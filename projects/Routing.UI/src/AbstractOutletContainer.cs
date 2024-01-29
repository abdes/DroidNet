// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.UI;

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing.UI.Contracts;

public abstract class AbstractOutletContainer : ObservableObject, IOutletContainer
{
    protected abstract Dictionary<string, object?> Outlets { get; }

    /// <inheritdoc />
    public void LoadContent(object viewModel, string? outletName = null)
    {
        outletName ??= nameof(Router.Outlet.Primary);
        if (!this.Outlets.TryGetValue(outletName, out _))
        {
            throw new ArgumentException($"unknown outlet name {outletName}", nameof(outletName));
        }

        this.Outlets[outletName] = viewModel;
        this.OnPropertyChanged((string?)this.GetPropertyNameForOutlet(outletName));
    }

    public object? GetViewModelForOutlet(string? outletName)
    {
        outletName ??= Router.Outlet.Primary;
        return this.Outlets.GetValueOrDefault(outletName);
    }

    public string GetPropertyNameForOutlet(string? outletName)
    {
        outletName ??= Router.Outlet.Primary;
        if (this.Outlets.ContainsKey(outletName))
        {
            return $"{outletName}ViewModel";
        }

        throw new ArgumentException($"unknown outlet name {outletName}", nameof(outletName));
    }
}
