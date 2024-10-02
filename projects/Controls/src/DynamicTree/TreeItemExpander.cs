// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public class TreeItemExpander : Control
{
    private bool isExpanded;

    public TreeItemExpander()
    {
        this.DefaultStyleKey = typeof(TreeItemExpander);

        this.Tapped += (_, __) => this.Toggle();
        this.DoubleTapped += (_, __) => this.Toggle();
    }

    public event EventHandler? Expand;

    public event EventHandler? Collapse;

    public bool IsExpanded
    {
        get => this.isExpanded;
        set
        {
            if (this.IsExpanded == value)
            {
                return;
            }

            this.isExpanded = value;
            this.UpdateVisualState();
        }
    }

    private void UpdateVisualState()
        => _ = VisualStateManager.GoToState(
            this,
            this.IsExpanded ? "Expanded" : "Collapsed",
            useTransitions: true);

    private void Toggle()
    {
        if (!this.IsExpanded)
        {
            this.Expand?.Invoke(this, EventArgs.Empty);
        }
        else
        {
            this.Collapse?.Invoke(this, EventArgs.Empty);
        }
    }
}
