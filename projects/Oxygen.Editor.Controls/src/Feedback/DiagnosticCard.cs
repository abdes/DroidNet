// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Controls;

/// <summary>
///     A finding shown as a card: its message, and the items it is about, a few inline and every one in place when the
///     card is expanded. The card's background is its severity's, so it needs no icon.
/// </summary>
[TemplatePart(Name = PreviewPartName, Type = typeof(TextBlock))]
[TemplatePart(Name = ExpandPartName, Type = typeof(Button))]
[TemplatePart(Name = DetailsPartName, Type = typeof(ItemsControl))]
[TemplateVisualState(GroupName = SeverityStates, Name = nameof(DiagnosticCardSeverity.Informational))]
[TemplateVisualState(GroupName = SeverityStates, Name = nameof(DiagnosticCardSeverity.Warning))]
[TemplateVisualState(GroupName = SeverityStates, Name = nameof(DiagnosticCardSeverity.Error))]
public sealed partial class DiagnosticCard : Control
{
    /// <summary>Identifies the <see cref="Severity"/> property.</summary>
    public static readonly DependencyProperty SeverityProperty = Register<DiagnosticCardSeverity>(nameof(Severity), DiagnosticCardSeverity.Informational);

    /// <summary>Identifies the <see cref="Message"/> property.</summary>
    public static readonly DependencyProperty MessageProperty = Register<string>(nameof(Message), string.Empty);

    /// <summary>Identifies the <see cref="Details"/> property.</summary>
    public static readonly DependencyProperty DetailsProperty = Register<IReadOnlyList<string>>(nameof(Details), defaultValue: null);

    /// <summary>Identifies the <see cref="PreviewCount"/> property.</summary>
    public static readonly DependencyProperty PreviewCountProperty = Register<int>(nameof(PreviewCount), 3);

    /// <summary>Identifies the <see cref="IsExpanded"/> property.</summary>
    public static readonly DependencyProperty IsExpandedProperty = Register<bool>(nameof(IsExpanded), defaultValue: false);

    private const string PreviewPartName = "PART_Preview";
    private const string ExpandPartName = "PART_Expand";
    private const string DetailsPartName = "PART_Details";
    private const string SeverityStates = "SeverityStates";

    private TextBlock? preview;
    private Button? expand;
    private ItemsControl? details;

    /// <summary>Initializes a new instance of the <see cref="DiagnosticCard"/> class.</summary>
    public DiagnosticCard()
    {
        this.DefaultStyleKey = typeof(DiagnosticCard);
    }

    /// <summary>Gets or sets how serious the finding is.</summary>
    public DiagnosticCardSeverity Severity { get => (DiagnosticCardSeverity)this.GetValue(SeverityProperty); set => this.SetValue(SeverityProperty, value); }

    /// <summary>Gets or sets the finding's message.</summary>
    public string Message { get => (string)this.GetValue(MessageProperty); set => this.SetValue(MessageProperty, value); }

    /// <summary>Gets or sets the items the finding is about, such as the names of the materials it affects; none for a
    /// finding about the whole subject.</summary>
    public IReadOnlyList<string>? Details { get => (IReadOnlyList<string>?)this.GetValue(DetailsProperty); set => this.SetValue(DetailsProperty, value); }

    /// <summary>Gets or sets how many items show inline; more make the card expandable.</summary>
    public int PreviewCount { get => (int)this.GetValue(PreviewCountProperty); set => this.SetValue(PreviewCountProperty, value); }

    /// <summary>Gets or sets a value indicating whether every item shows, one per line.</summary>
    public bool IsExpanded { get => (bool)this.GetValue(IsExpandedProperty); set => this.SetValue(IsExpandedProperty, value); }

    /// <summary>Gets the inline line of a list of items: every item when there are few, else the first ones and how
    /// many more there are.</summary>
    /// <param name="items">The items.</param>
    /// <param name="previewCount">How many items show inline.</param>
    /// <returns>The line, such as "Bun, Cheese, Sauce and 2 more"; empty without items.</returns>
    public static string PreviewOf(IReadOnlyList<string> items, int previewCount)
    {
        ArgumentNullException.ThrowIfNull(items);
        var shown = Math.Max(1, previewCount);
        return items.Count switch
        {
            0 => string.Empty,
            1 => items[0],
            _ when items.Count <= shown => string.Join(", ", items.Take(items.Count - 1)) + " and " + items[^1],
            _ => string.Join(", ", items.Take(shown)) + string.Create(CultureInfo.CurrentCulture, $" and {items.Count - shown} more"),
        };
    }

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        if (this.expand is not null)
        {
            this.expand.Click -= this.OnExpandClick;
        }

        base.OnApplyTemplate();
        this.preview = this.GetTemplateChild(PreviewPartName) as TextBlock;
        this.expand = this.GetTemplateChild(ExpandPartName) as Button;
        this.details = this.GetTemplateChild(DetailsPartName) as ItemsControl;
        if (this.expand is not null)
        {
            this.expand.Click += this.OnExpandClick;
        }

        this.Update(useTransitions: false);
    }

    private static DependencyProperty Register<T>(string name, object? defaultValue)
        => DependencyProperty.Register(name, typeof(T), typeof(DiagnosticCard), new PropertyMetadata(defaultValue, static (sender, _) => ((DiagnosticCard)sender).Update(useTransitions: true)));

    private void OnExpandClick(object sender, RoutedEventArgs args) => this.IsExpanded = !this.IsExpanded;

    private void Update(bool useTransitions)
    {
        _ = VisualStateManager.GoToState(this, this.Severity.ToString(), useTransitions);
        AutomationProperties.SetName(this, $"{this.Severity}: {this.Message}");
        var items = this.Details ?? [];
        var expandable = items.Count > Math.Max(1, this.PreviewCount);
        var expanded = expandable && this.IsExpanded;
        if (this.preview is not null)
        {
            this.preview.Text = PreviewOf(items, this.PreviewCount);
            this.preview.Visibility = items.Count != 0 && !expanded ? Visibility.Visible : Visibility.Collapsed;
        }

        if (this.details is not null)
        {
            this.details.ItemsSource = expanded ? items : null;
            this.details.Visibility = expanded ? Visibility.Visible : Visibility.Collapsed;
        }

        if (this.expand is not null)
        {
            this.expand.Visibility = expandable ? Visibility.Visible : Visibility.Collapsed;
            this.expand.Content = new FontIcon { Glyph = expanded ? "" : "", FontSize = 12 };
            var label = expanded ? "Show fewer" : string.Create(CultureInfo.CurrentCulture, $"Show all {items.Count}");
            ToolTipService.SetToolTip(this.expand, label);
            AutomationProperties.SetName(this.expand, label);
        }
    }
}
