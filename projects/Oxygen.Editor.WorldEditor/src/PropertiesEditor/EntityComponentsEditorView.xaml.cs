// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Oxygen.Editor.Core;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.
namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

[ViewModel(typeof(EntityComponentsEditorViewModel))]
public sealed partial class EntityComponentsEditorView : UserControl
{
    private string? originalName;
    private bool newNameIsValid;

    /// <summary>
    /// Initializes a new instance of the <see cref="EntityComponentsEditorView"/> class.
    /// </summary>
    public EntityComponentsEditorView()
    {
        this.InitializeComponent();

        this.ViewModelChanged += (_, _) =>
        {
            if (this.ViewModel is null)
            {
                return;
            }

            this.Resources["VmToViewConverter"] = this.ViewModel.VmToViewConverter;
        };
    }

    private void EntityNameTextBlock_DoubleTapped(object sender, DoubleTappedRoutedEventArgs e)
    {
        e.Handled = true;
        this.StartRename();
    }

    private void StartRename()
    {
        this.originalName = this.EntityNameTextBlock.Text;
        this.EntityNameTextBlock.Visibility = Visibility.Collapsed;
        this.RenameTextBox.Text = this.EntityNameTextBlock.Text;
        this.ValidateName();

        this.RenameTextBox.Visibility = Visibility.Visible;
        this.RenameTextBox.SelectAll();
        _ = this.RenameTextBox.Focus(FocusState.Programmatic);

        this.RenameTextBox.TextChanged += this.RenameTextBox_TextChanged;
        this.RenameTextBox.LostFocus += this.RenameTextBox_LostFocus;
        this.RenameTextBox.KeyDown += this.RenameTextBox_KeyDown;
        this.RenameTextBox.GotFocus += this.RenameTextBox_GotFocus;
    }

    private void EndRename()
    {
        this.RenameTextBox.TextChanged -= this.RenameTextBox_TextChanged;
        this.RenameTextBox.LostFocus -= this.RenameTextBox_LostFocus;
        this.RenameTextBox.KeyDown -= this.RenameTextBox_KeyDown;
        this.RenameTextBox.GotFocus -= this.RenameTextBox_GotFocus;

        this.RenameTextBox.Visibility = Visibility.Collapsed;
        this.EntityNameTextBlock.Visibility = Visibility.Visible;
        this.EntityNameError.Visibility = Visibility.Collapsed;
    }

    private void TryCommitRename()
    {
        Debug.Assert(this.newNameIsValid, "new name must be valid before trying to commit");

        var newName = this.RenameTextBox.Text.Trim();
        if (this.originalName?.Equals(newName, StringComparison.Ordinal) == false)
        {
            this.ViewModel?.RenameItemsCommand.Execute(newName);
        }

        this.EndRename();
    }

    private void CancelRename()
    {
        this.RenameTextBox.Text = this.originalName;
        this.EndRename();
    }

    private void RenameTextBox_TextChanged(object sender, TextChangedEventArgs e) => this.ValidateName();

    private void ValidateName()
    {
        this.newNameIsValid = InputValidation.IsValidFileName(this.RenameTextBox.Text.Trim());
        this.EntityNameError.Visibility = this.newNameIsValid ? Visibility.Collapsed : Visibility.Visible;
    }

    private void RenameTextBox_KeyDown(object sender, KeyRoutedEventArgs e)
    {
        // ReSharper disable once SwitchStatementMissingSomeEnumCasesNoDefault (only interested in these keys)
        switch (e.Key)
        {
            case Windows.System.VirtualKey.Enter when !this.newNameIsValid:
                return;
            case Windows.System.VirtualKey.Enter:
                this.TryCommitRename();
                e.Handled = true;
                break;

            case Windows.System.VirtualKey.Escape:
                this.CancelRename();
                e.Handled = true;
                break;
        }
    }

    private void RenameTextBox_LostFocus(object sender, RoutedEventArgs e)
    {
        if (this.newNameIsValid)
        {
            this.TryCommitRename();
        }
        else
        {
            this.CancelRename();
        }
    }

    private void RenameTextBox_GotFocus(object sender, RoutedEventArgs e)
    {
        var textBox = sender as TextBox;
        if (textBox != null)
        {
            this.RenameTextBox.SelectAll();
            this.originalName = textBox.Text;
        }
    }
}
