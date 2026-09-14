// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using CommunityToolkit.WinUI;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.ContentBrowser.Importing;

/// <summary>Compact model import review content hosted by the existing dialog service.</summary>
[ViewModel(typeof(SceneImportDialogViewModel))]
public sealed partial class SceneImportDialogView
{
    private SceneImportDialogViewModel? subscribedModel;

    /// <summary>Initializes a new instance of the <see cref="SceneImportDialogView"/> class.</summary>
    public SceneImportDialogView()
    {
        this.InitializeComponent();
        this.Loaded += (_, _) => this.UpdateSubscription();
        this.Unloaded += (_, _) => this.UpdateSubscription();
        this.ViewModelChanged += (_, _) => this.UpdateSubscription();
    }

    private void UpdateSubscription()
    {
        this.subscribedModel?.PropertyChanged -= this.OnModelChanged;

        this.subscribedModel = this.IsLoaded ? this.ViewModel : null;
        this.subscribedModel?.PropertyChanged += this.OnModelChanged;

        this.UpdateButton();
    }

    private void OnModelChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(SceneImportDialogViewModel.CanAccept), StringComparison.Ordinal))
        {
            this.UpdateButton();
        }
    }

    private void UpdateButton()
    {
        if (this.FindAscendant<ContentDialog>() is { } dialog)
        {
            dialog.IsPrimaryButtonEnabled = this.ViewModel?.CanAccept == true;
        }
    }
}
