// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Controls;

using System.ComponentModel;
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

        // Listen for future changes to the ViewModel or the VmToViewConverter
        // properties to update the outlet content as soon as possible.
        this.PropertyChanged += this.OnNeedUpdateContent;

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

            // Update the content on Loaded only if it has not been already updated.
            if (this.OutletContent is null)
            {
                this.UpdateContent();
            }
        };

        this.Unloaded += (_, _) => this.PropertyChanged -= this.OnNeedUpdateContent;
    }

    public string? Outlet { get; set; }

    public bool IsActivated => this.ViewModel is not null;

    public bool IsActivatedWithNoContent => this.IsActivated && !this.IsPopulated;

    public bool IsPopulated => this.OutletContent is not null;

    private void OnNeedUpdateContent(object? sender, PropertyChangedEventArgs args)
    {
        _ = sender; // unused

        if (args.PropertyName is not null &&
            (args.PropertyName.Equals(nameof(this.ViewModel), StringComparison.Ordinal) ||
             args.PropertyName.Equals(nameof(this.VmToViewConverter), StringComparison.Ordinal)))
        {
            this.UpdateContent();
        }
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
