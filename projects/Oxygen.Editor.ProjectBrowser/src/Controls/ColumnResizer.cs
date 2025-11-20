// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
///     A custom control that provides a drag handle for resizing a column's width.
/// </summary>
/// <remarks>
///     The <see cref="ColumnResizer"/> is designed to be placed within a layout (typically a Grid)
///     to allow users to adjust the width of a column. It uses a <see cref="Thumb"/> primitive to
///     capture drag interactions and updates the <see cref="TargetWidth"/> property accordingly.
///     <para>
///     <strong>Usage Model:</strong>
///     <list type="bullet">
///       <item>
///         <description>Place the control in the visual tree where the resize handle should appear
///         (e.g., at the edge of a column).</description>
///       </item>
///       <item>
///         <description>Bind the <see cref="TargetWidth"/> property to the source of truth for the
///         column's width (e.g., a ViewModel property or a GridLength value via a
///         converter).</description>
///       </item>
///       <item>
///         <description>Optionally set the <see cref="MinimumWidth"/> to prevent the column from
///         becoming too narrow.</description>
///       </item>
///       <item>
///         <description>Subscribe to the <see cref="ResizeCompleted"/> event to perform actions
///         when the user finishes dragging (e.g., persisting the new width).</description>
///       </item>
///     </list>
///     </para>
/// </remarks>
[TemplatePart(Name = PartThumb, Type = typeof(Thumb))]
public sealed class ColumnResizer : Control
{
    /// <summary>
    /// Identifies the <see cref="TargetWidth"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty TargetWidthProperty =
        DependencyProperty.Register(nameof(TargetWidth), typeof(double), typeof(ColumnResizer), new PropertyMetadata(0.0));

    /// <summary>
    /// Identifies the <see cref="MinimumWidth"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty MinimumWidthProperty =
        DependencyProperty.Register(nameof(MinimumWidth), typeof(double), typeof(ColumnResizer), new PropertyMetadata(48.0));

    private const string PartThumb = "PART_Thumb";
    private Thumb? thumb;

    /// <summary>
    /// Initializes a new instance of the <see cref="ColumnResizer"/> class.
    /// </summary>
    public ColumnResizer()
    {
        this.DefaultStyleKey = typeof(ColumnResizer);
        this.Loaded += this.OnLoaded;
    }

    /// <summary>
    /// Occurs when the resizing operation is completed.
    /// </summary>
    public event EventHandler<EventArgs>? ResizeCompleted;

    /// <summary>
    /// Gets or sets the target width to be resized.
    /// </summary>
    public double TargetWidth
    {
        get => (double)this.GetValue(TargetWidthProperty);
        set => this.SetValue(TargetWidthProperty, value);
    }

    /// <summary>
    /// Gets or sets the minimum width allowed for the column.
    /// </summary>
    public double MinimumWidth
    {
        get => (double)this.GetValue(MinimumWidthProperty);
        set => this.SetValue(MinimumWidthProperty, value);
    }

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        if (this.thumb != null)
        {
            this.thumb.DragDelta -= this.OnDragDelta;
            this.thumb.DragCompleted -= this.OnDragCompleted;
        }

        this.thumb = this.GetTemplateChild(PartThumb) as Thumb;

        if (this.thumb != null)
        {
            this.thumb.DragDelta += this.OnDragDelta;
            this.thumb.DragCompleted += this.OnDragCompleted;
        }
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        this.ProtectedCursor = InputSystemCursor.Create(InputSystemCursorShape.SizeWestEast);
    }

    private void OnDragDelta(object sender, DragDeltaEventArgs e)
    {
        var newWidth = Math.Max(this.MinimumWidth, this.TargetWidth + e.HorizontalChange);
        if (Math.Abs(newWidth - this.TargetWidth) >= 1.0)
        {
            this.TargetWidth = newWidth;
        }
    }

    private void OnDragCompleted(object sender, DragCompletedEventArgs e)
    {
        this.ResizeCompleted?.Invoke(this, EventArgs.Empty);
    }
}
