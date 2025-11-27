// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace DroidNet.Controls;

/// <summary>
/// Represents a toggle button control for use in a toolbar, supporting icon, label, and compact visual states.
/// </summary>
[TemplatePart(Name = LayoutGridPartName, Type = typeof(Grid))]
[TemplatePart(Name = LayoutGridPartName, Type = typeof(Grid))]
[TemplatePart(Name = IconPresenterPartName, Type = typeof(IconSourceElement))]
[TemplatePart(Name = LabelTextPartName, Type = typeof(TextBlock))]
public partial class ToolBarToggleButton : ToggleButton
{
    private const string LayoutGridPartName = "PartLayoutGrid";
    private const string IconPresenterPartName = "IconPresenter";
    private const string LabelTextPartName = "LabelText";

    private Grid? layoutGrid;
    private IconSourceElement? iconPresenter;
    private TextBlock? labelText;

    private ILogger? logger;

    /// <summary>
    /// Initializes a new instance of the <see cref="ToolBarToggleButton"/> class.
    /// </summary>
    public ToolBarToggleButton()
    {
        this.DefaultStyleKey = typeof(ToolBarToggleButton);
        this.Loaded += (s, e) => this.UpdateLabelPosition();
    }

    /// <summary>
    /// Updates the label position and layout based on the <see cref="ToolBarLabelPosition"/> property.
    /// </summary>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MA0051:Method is too long", Justification = "keep the logic together")]
    internal void UpdateLabelPosition()
    {
        if (this.layoutGrid == null || this.labelText == null || this.iconPresenter == null)
        {
            return;
        }

        // Determine effective position
        var position = this.ToolBarLabelPosition;
        if (position == ToolBarLabelPosition.Auto)
        {
            // Use CommunityToolkit.WinUI extension to find ancestor in the visual/logical tree
            var parent = this.FindAscendant<ToolBar>();
            position = parent?.DefaultLabelPosition ?? ToolBarLabelPosition.Right;
        }

        this.LogLabelPositionUpdated(position.ToString(), this.IsLabelVisible);

        // Reset Grid
        this.layoutGrid.ColumnDefinitions.Clear();
        this.layoutGrid.RowDefinitions.Clear();
        Grid.SetColumn(this.iconPresenter, 0);
        Grid.SetRow(this.iconPresenter, 0);
        Grid.SetColumn(this.labelText, 0);
        Grid.SetRow(this.labelText, 0);

        // Apply Layout
        switch (position)
        {
            case ToolBarLabelPosition.Collapsed:
                this.IsLabelVisible = false;
                this.layoutGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                break;

            case ToolBarLabelPosition.Right:
                this.IsLabelVisible = true;
                this.layoutGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                this.layoutGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                Grid.SetColumn(this.labelText, 1);
                break;

            case ToolBarLabelPosition.Left:
                this.IsLabelVisible = true;
                this.layoutGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                this.layoutGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                Grid.SetColumn(this.labelText, 0);
                Grid.SetColumn(this.iconPresenter, 1);
                break;

            case ToolBarLabelPosition.Bottom:
                this.IsLabelVisible = true;
                this.layoutGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                this.layoutGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                Grid.SetRow(this.labelText, 1);
                break;

            case ToolBarLabelPosition.Top:
                this.IsLabelVisible = true;
                this.layoutGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                this.layoutGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                Grid.SetRow(this.labelText, 0);
                Grid.SetRow(this.iconPresenter, 1);
                break;
        }
    }

    /// <summary>
    /// Applies the control template and initializes template parts.
    /// </summary>
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        this.layoutGrid = this.GetTemplateChild(LayoutGridPartName) as Grid;
        this.iconPresenter = this.GetTemplateChild(IconPresenterPartName) as IconSourceElement;
        this.labelText = this.GetTemplateChild(LabelTextPartName) as TextBlock;
        this.UpdateLabelPosition();
        this.UpdateVisualState();
    }

    /// <summary>
    /// Updates the visual state of the button based on the <see cref="IsCompact"/> property.
    /// </summary>
    private void UpdateVisualState()
    {
        var state = this.IsCompact ? "Compact" : "Standard";
        this.LogVisualStateUpdated(state);
        _ = VisualStateManager.GoToState(this, state, useTransitions: true);
    }
}
