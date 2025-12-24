// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using CommunityToolkit.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>
///     View for the Local Folder mount dialog.
/// </summary>
[ViewModel(typeof(LocalFolderMountDialogViewModel))]
public sealed partial class LocalFolderMountDialogView
{
    private ContentDialog? hostDialog;

    /// <summary>
    ///     Initializes a new instance of the <see cref="LocalFolderMountDialogView"/> class.
    /// </summary>
    public LocalFolderMountDialogView()
    {
        this.InitializeComponent();

        this.ViewModelChanged += this.OnViewModelChanged;

        this.Loaded += (_, _) =>
        {
            this.hostDialog ??= this.FindAscendant<ContentDialog>();
            this.UpdatePrimaryButtonEnabled();
        };
    }

    private void OnViewModelChanged(object? sender, ViewModelChangedEventArgs<LocalFolderMountDialogViewModel> args)
    {
        if (args.OldValue is not null)
        {
            args.OldValue.PropertyChanged -= this.OnViewModelPropertyChanged;
        }

        if (this.ViewModel is not null)
        {
            this.ViewModel.PropertyChanged += this.OnViewModelPropertyChanged;
        }

        this.Bindings.Update();
        this.UpdatePrimaryButtonEnabled();
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(LocalFolderMountDialogViewModel.CanAccept), StringComparison.Ordinal))
        {
            _ = this.DispatcherQueue.TryEnqueue(this.UpdatePrimaryButtonEnabled);
        }
    }

    private void UpdatePrimaryButtonEnabled()
    {
        this.hostDialog ??= this.FindAscendant<ContentDialog>();
        if (this.hostDialog is not null && this.ViewModel is not null)
        {
            this.hostDialog.IsPrimaryButtonEnabled = this.ViewModel.CanAccept;
        }
    }
}
