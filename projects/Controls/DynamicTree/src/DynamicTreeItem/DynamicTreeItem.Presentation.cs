// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls;

/// <summary>Consumer-owned trailing presentation and gesture isolation for tree rows.</summary>
public partial class DynamicTreeItem
{
    /// <summary>Gets a value indicating whether an interactive trailing-content pointer gesture is in progress.</summary>
    internal bool IsInteractiveActionInProgress => this.isInteractivePointerActive;

    /// <summary>Checks whether an event source belongs to an interactive control in the consumer's trailing content.</summary>
    /// <param name="element">The input event source.</param>
    /// <returns>True for enabled buttons or focusable controls and their descendants within the trailing presenter.</returns>
    internal bool IsInteractiveContentElement(DependencyObject? element)
    {
        if (this.GetTemplateChild(TrailingContentPresenterPart) is not DependencyObject trailingContent)
        {
            return false;
        }

        var isInteractive = false;
        for (var current = element; current is not null; current = VisualTreeHelper.GetParent(current))
        {
            if (ReferenceEquals(current, trailingContent))
            {
                return isInteractive;
            }

            isInteractive |= current is Control { IsEnabled: true, IsTabStop: true } or ButtonBase { IsEnabled: true };
        }

        return false;
    }

    /// <summary>Applies the consumer-selected template to a realized logical item.</summary>
    /// <param name="selector">The consumer template selector.</param>
    /// <param name="item">The logical row identity, or null during recycling.</param>
    /// <param name="template">A uniform template that takes precedence over the selector.</param>
    internal void UpdateTrailingContentTemplate(DataTemplateSelector? selector, ITreeItem? item, DataTemplate? template = null)
    {
        this.TrailingContent = item;
        this.TrailingContentTemplate = item is null ? null : template ?? selector?.SelectTemplate(item, this);
        this.UpdateTrailingContentPresenter();
    }

    /// <summary>Updates the optional aligned trailing column width.</summary>
    /// <param name="width">The column width, in DIPs.</param>
    internal void UpdateTrailingContentWidth(double width) => this.TrailingContentWidth = Math.Max(0, width);

    /// <summary>Re-measures trailing content independently of the hierarchy's indentation.</summary>
    internal void UpdateTrailingContentPresenter()
    {
        if (this.trailingContentPresenter is null || this.trailingContentColumn is null)
        {
            return;
        }

        var hasContent = this.TrailingContentTemplate is not null;
        this.trailingContentPresenter.Visibility = hasContent ? Visibility.Visible : Visibility.Collapsed;
        this.trailingContentColumn.Width = this.TrailingContentWidth > 0
            ? new GridLength(this.TrailingContentWidth)
            : hasContent ? GridLength.Auto : new GridLength(0);
        this.InvalidateMeasure();
    }

    /// <summary>Updates the noninteractive drop-into target indicator.</summary>
    /// <param name="position">The current drop intent.</param>
    internal void UpdateDropIndicatorVisual(DynamicTree.DropIndicatorPosition position)
    {
        if (this.dropIntoIndicator is { } indicator)
        {
            indicator.Visibility = position == DynamicTree.DropIndicatorPosition.Inside ? Visibility.Visible : Visibility.Collapsed;
        }
    }
}
