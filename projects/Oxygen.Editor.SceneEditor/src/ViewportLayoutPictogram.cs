// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.WorldEditor.SceneEditor;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// A small drawing of a pane arrangement: one cell per pane, placed as the scene editor places its
/// viewports, with the main pane in the accent color.
/// </summary>
public sealed partial class ViewportLayoutPictogram : Grid
{
    /// <summary>Identifies the <see cref="Layout"/> dependency property.</summary>
    public static readonly DependencyProperty LayoutProperty = DependencyProperty.Register(
        nameof(Layout),
        typeof(SceneViewLayout),
        typeof(ViewportLayoutPictogram),
        new PropertyMetadata(SceneViewLayout.OnePane, (sender, _) => ((ViewportLayoutPictogram)sender).Rebuild()));

    /// <summary>Initializes a new instance of the <see cref="ViewportLayoutPictogram"/> class.</summary>
    public ViewportLayoutPictogram()
    {
        this.Width = 36;
        this.Height = 26;
        this.RowSpacing = 2;
        this.ColumnSpacing = 2;
        this.Rebuild();
    }

    /// <summary>Gets or sets the drawn arrangement.</summary>
    public SceneViewLayout Layout
    {
        get => (SceneViewLayout)this.GetValue(LayoutProperty);
        set => this.SetValue(LayoutProperty, value);
    }

    private static Brush GetBrush(string key) => (Brush)Application.Current.Resources[key];

    private void Rebuild()
    {
        this.Children.Clear();
        this.RowDefinitions.Clear();
        this.ColumnDefinitions.Clear();

        var (rows, columns) = SceneLayoutHelpers.GetGridDimensions(this.Layout);
        for (var i = 0; i < rows; i++)
        {
            this.RowDefinitions.Add(new RowDefinition());
        }

        for (var i = 0; i < columns; i++)
        {
            this.ColumnDefinitions.Add(new ColumnDefinition());
        }

        var placements = SceneLayoutHelpers.GetPlacements(this.Layout);
        for (var i = 0; i < placements.Count; i++)
        {
            var (row, column, rowSpan, columnSpan) = placements[i];
            var cell = new Border
            {
                CornerRadius = new CornerRadius(1),
                Background = GetBrush(i == 0 ? "AccentFillColorDefaultBrush" : "ControlStrongFillColorDefaultBrush"),
            };
            SetRow(cell, row);
            SetColumn(cell, column);
            SetRowSpan(cell, rowSpan);
            SetColumnSpan(cell, columnSpan);
            this.Children.Add(cell);
        }
    }
}
