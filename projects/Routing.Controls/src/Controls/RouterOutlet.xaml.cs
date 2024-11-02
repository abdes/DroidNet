// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Controls;

using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

/// <summary>
/// A content control representing a router outlet; i.e. the place in a view
/// where the content of a route's view model can be rendered.
/// </summary>
[ObservableObject]
public sealed partial class RouterOutlet
{
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsPopulated))]
    [NotifyPropertyChangedFor(nameof(IsActivatedWithNoContent))]
    private object? outletContent;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsActivated))]
    [NotifyPropertyChangedFor(nameof(IsActivatedWithNoContent))]
    private object? viewModel;

    [ObservableProperty]
    private IValueConverter? vmToViewConverter;

    public RouterOutlet()
    {
        this.InitializeComponent();

        this.Loaded += (_, _) =>
        {
            if (this.VmToViewConverter is null)
            {
                if (!Application.Current.Resources.TryGetValue("VmToViewConverter", out var converter))
                {
                    return;
                }

                this.vmToViewConverter = (IValueConverter)converter;
            }

            this.UpdateContent();
        };
    }

    public string? Outlet { get; set; }

    public bool IsActivated => this.ViewModel is not null;

    public bool IsActivatedWithNoContent => this.IsActivated && !this.IsPopulated;

    public bool IsPopulated => this.OutletContent is not null;

    partial void OnViewModelChanged(object? value)
    {
        _ = value; // Avoid parameter unused warning
        this.UpdateContent();
    }

    partial void OnVmToViewConverterChanged(IValueConverter? value)
    {
        _ = value; // Avoid parameter unused warning
        this.UpdateContent();
    }

    private void UpdateContent()
    {
        if (this.ViewModel is null)
        {
            return;
        }

        if (this.VmToViewConverter is null)
        {
            return;
        }

        this.OutletContent = this.VmToViewConverter.Convert(
            this.ViewModel,
            typeof(object),
            parameter: null,
            language: null);
    }
}
