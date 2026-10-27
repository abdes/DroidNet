// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Oxygen.Editor.Controls.PropertyEditors;

namespace Oxygen.Editor.Controls;

/// <content>
/// The context menu of an overridden row (<see cref="OverrideActionMenu"/>): <i>Reset</i>, with the
/// <see cref="ResetToolTip"/> as its text, then the commands <see cref="OverrideActions"/> supplies, such as
/// <c>Apply to Watchtower</c>. A row whose value overrides nothing keeps the menu of what was clicked.
/// </content>
public partial class PropertyCard
{
    /// <summary>Identifies the source of the commands the row's context menu offers besides reset.</summary>
    public static readonly DependencyProperty OverrideActionsProperty = DependencyProperty.Register(
        nameof(OverrideActions), typeof(IOverrideActionSource), typeof(PropertyCard), new PropertyMetadata(defaultValue: null));

    /// <summary>Gets or sets the source of the commands the row's context menu offers while the value is overridden.</summary>
    public IOverrideActionSource? OverrideActions
    {
        get => (IOverrideActionSource?)this.GetValue(OverrideActionsProperty);
        set => this.SetValue(OverrideActionsProperty, value);
    }

    private void OnContextRequested(UIElement sender, ContextRequestedEventArgs args)
    {
        if (!args.Handled && this.OverrideState != OverrideState.None && (this.ResetCommand is not null || this.OverrideActions is not null))
        {
            _ = OverrideActionMenu.ShowAsync(this, args, this.OverrideActions, this.ResetToolTip, this.ResetCommand);
        }
    }
}
