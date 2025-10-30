// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls;

/// <summary>
///     Represents a single tab item in a TabStrip control.
/// </summary>
[TemplatePart(Name = RootGridPartName, Type = typeof(Grid))]
[TemplatePart(Name = IconPartName, Type = typeof(ContentPresenter))]
[TemplatePart(Name = HeaderPartName, Type = typeof(TextBlock))]
[TemplatePart(Name = ButtonsContainerPartName, Type = typeof(StackPanel))]
[TemplatePart(Name = PinButtonPartName, Type = typeof(Button))]
[TemplatePart(Name = CloseButtonPartName, Type = typeof(Button))]
[TemplateVisualState(Name = NormalVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = PointerOverVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = SelectedVisualState, GroupName = CommonVisualStates)]
[TemplateVisualState(Name = SelectedPointerOverVisualState, GroupName = CommonVisualStates)]
[SuppressMessage("Design", "CA1001:Types that own disposable fields should be disposable", Justification = "Dispatcher-backed proxy fields are disposed in the Unloaded handler to align with control lifetime.")]

public partial class TabStripItem : ContentControl
{
    /// <summary>The name of the root grid template part.</summary>
    public const string RootGridPartName = "PartRootGrid";

    /// <summary>The name of the icon template part.</summary>
    public const string IconPartName = "PartIcon";

    /// <summary>The name of the header template part.</summary>
    public const string HeaderPartName = "PartHeader";

    /// <summary>The name of the buttons container template part.</summary>
    public const string ButtonsContainerPartName = "ButtonsContainer";

    /// <summary>The name of the pin button template part.</summary>
    public const string PinButtonPartName = "PartPinButton";

    /// <summary>The name of the close button template part.</summary>
    public const string CloseButtonPartName = "PartCloseButton";

    /// <summary>The name of the common visual states group.</summary>
    public const string CommonVisualStates = "CommonStates";

    /// <summary>The name of the normal visual state.</summary>
    public const string NormalVisualState = "Normal";

    /// <summary>The name of the pointer over visual state.</summary>
    public const string PointerOverVisualState = "PointerOver";

    /// <summary>The name of the selected visual state.</summary>
    public const string SelectedVisualState = "Selected";

    /// <summary>The name of the selected pointer over visual state.</summary>
    public const string SelectedPointerOverVisualState = "SelectedPointerOver";

    /// <summary>
    ///     Minimum space that must remain for dragging the tab, even when the tool buttons are overlayed.
    /// </summary>
    internal const int MinDragWidth = 40;

    // Required parts will not be nulll after OnApplyTemplate is called
    private StackPanel buttonsContainer = null!;
    private Button pinButton = null!;
    private Button closeButton = null!;

    private ContentPresenter? iconPart;
    private TextBlock? headerPart;
    private ILogger? logger;
    private bool isPointerOver;

    /// <summary>
    ///     Initializes a new instance of the <see cref="TabStripItem"/> class.
    /// </summary>
    public TabStripItem()
    {
        this.DefaultStyleKey = typeof(TabStripItem);
    }

    /// <summary>
    ///     Applies the control template and sets up required and optional template parts.
    /// </summary>
    /// <exception cref="InvalidOperationException">Thrown if a required template part is missing or of the wrong type.</exception>
    [MemberNotNull(nameof(buttonsContainer), nameof(pinButton), nameof(closeButton))]
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        this.LogApplyingTemplate();

        T? GetTemplatePart<T>(string name, bool isRequired = false)
            where T : DependencyObject
        {
            var part = this.GetTemplateChild(name) as T;
            if (part is null)
            {
                var expectedType = typeof(T);
                this.LogTemplatePartNotFound(name, expectedType, isRequired);
                if (isRequired)
                {
                    throw new InvalidOperationException($"The required template part '{name}' is missing or is not of type '{expectedType}'.");
                }
            }

            return part;
        }

        T GetRequiredTemplatePart<T>(string name)
            where T : DependencyObject
            => GetTemplatePart<T>(name, isRequired: true)!;

        // Required parts
        this.buttonsContainer = GetRequiredTemplatePart<StackPanel>(ButtonsContainerPartName);
        this.pinButton = GetRequiredTemplatePart<Button>(PinButtonPartName);
        this.closeButton = GetRequiredTemplatePart<Button>(CloseButtonPartName);

        // Optional parts
        this.iconPart = GetTemplatePart<ContentPresenter>(IconPartName);
        this.headerPart = GetTemplatePart<TextBlock>(HeaderPartName);

        // Attach button click events for the tool buttons
        this.pinButton.Click -= PinButton_Click;
        this.pinButton.Click += PinButton_Click;
        this.closeButton.Click -= CloseButton_Click;
        this.closeButton.Click += CloseButton_Click;

        this.UpdateToolBarVisibility();
        this.UpdateMinWidth();
        this.UpdateVisualStates(useTransitions: false);

        void PinButton_Click(object sender, RoutedEventArgs e) => this.OnPinClicked();
        void CloseButton_Click(object sender, RoutedEventArgs e) => this.OnCloseClicked();
    }

    /// <summary>
    ///     Handles pointer entered events and updates visual state.
    /// </summary>
    /// <param name="e">Pointer event data.</param>
    protected override void OnPointerEntered(PointerRoutedEventArgs e)
    {
        base.OnPointerEntered(e);
        this.LogPointerEntered(e);
        this.OnPointerEntered();
    }

    /// <summary>
    ///     Called when the pointer enters the control. Updates state and toolbar visibility.
    /// </summary>
    protected virtual void OnPointerEntered()
    {
        this.isPointerOver = true;
        this.UpdateVisualStates(useTransitions: true);

        // Show buttons in compact mode on hover
        if (this.IsCompact)
        {
            this.buttonsContainer.Visibility = Visibility.Visible;
        }
    }

    /// <summary>
    ///     Handles pointer exited events and updates visual state.
    /// </summary>
    /// <param name="e">Pointer event data.</param>
    protected override void OnPointerExited(PointerRoutedEventArgs e)
    {
        base.OnPointerExited(e);
        this.LogPointerExited();
        this.OnPointerExited();
    }

    /// <summary>
    ///     Called when the pointer exits the control. Updates state and toolbar visibility.
    /// </summary>
    protected virtual void OnPointerExited()
    {
        this.isPointerOver = false;
        this.UpdateVisualStates(useTransitions: true);

        // Hide buttons in compact mode on exit
        if (this.IsCompact)
        {
            this.buttonsContainer.Visibility = Visibility.Collapsed;
        }
    }

    /// <summary>
    ///     Called when the pin button is clicked. Toggles the pinned state.
    /// </summary>
    protected virtual void OnPinClicked()
    {
        if (this.Item is null)
        {
            return;
        }

        this.Item.IsPinned = !this.Item.IsPinned;
        this.LogPinClicked();
    }

    /// <summary>
    ///     Called when the close button is clicked. Raises the close requested event.
    /// </summary>
    protected virtual void OnCloseClicked()
    {
        if (this.Item is null)
        {
            return;
        }

        this.CloseRequested?.Invoke(this, new TabCloseRequestedEventArgs { Item = this.Item });
        this.LogCloseRequested();
    }

    /// <summary>
    ///     Handles changes to the Item property and updates bindings and state.
    /// </summary>
    /// <param name="e">Dependency property change event data.</param>
    private void OnItemChanged(DependencyPropertyChangedEventArgs e)
    {
        this.LogItemChanged(e);

        this.DataContext = e.NewValue; // For bindings
        this.Content = e.NewValue; // For ContentPresenter if needed

        if (e.OldValue is TabItem oldItem)
        {
            oldItem.PropertyChanged -= this.TabItem_OnPropertyChanged;
        }

        if (e.NewValue is not TabItem newItem)
        {
            // With no Item, the control is useless
            this.IsEnabled = false;
            this.LogEnabledOrDisabled();
            return;
        }

        this.IsEnabled = true;
        this.LogEnabledOrDisabled();
        newItem.PropertyChanged += this.TabItem_OnPropertyChanged;
        this.UpdateVisualStates(useTransitions: false);
    }

    /// <summary>
    ///     Handles property changes on the bound TabItem and updates visual state as needed.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="e">Property changed event data.</param>
    private void TabItem_OnPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(TabItem.IsSelected), System.StringComparison.Ordinal))
        {
            this.LogItemPropertyChanged(e);
            this.UpdateVisualStates(useTransitions: true);
        }
        else if (string.Equals(e.PropertyName, nameof(TabItem.IsPinned), System.StringComparison.Ordinal))
        {
            this.LogItemPropertyChanged(e);
            this.UpdateVisualStates(useTransitions: true);
        }
    }

    /// <summary>
    ///     Updates the visual states of the control based on selection and pointer state.
    /// </summary>
    /// <param name="useTransitions">Whether to use visual transitions.</param>
    private void UpdateVisualStates(bool useTransitions)
    {
        if (this.Item is null)
        {
            // An item being recycled may temporarily have no Item
            return;
        }

        var basicState = this.Item.IsSelected
            ? this.isPointerOver ? SelectedPointerOverVisualState : SelectedVisualState
            : this.isPointerOver ? PointerOverVisualState : NormalVisualState;

        this.LogVisualState(basicState);
        _ = VisualStateManager.GoToState(this, basicState, useTransitions);

        var pinState = this.Item?.IsPinned == true ? "Pinned" : "Unpinned";
        _ = VisualStateManager.GoToState(this, pinState, useTransitions: false);
    }

    /// <summary>
    ///     Updates the minimum width of the tab item based on button and margin sizes.
    /// </summary>
    private void UpdateMinWidth()
    {
        var marginLeft = this.buttonsContainer?.Margin.Left ?? 0;
        var marginRight = this.buttonsContainer?.Margin.Right ?? 0;
        var pinWidth = this.pinButton?.Width ?? 0;
        var closeWidth = this.closeButton?.Width ?? 0;
        var spacing = (pinWidth > 0 && closeWidth > 0) ? this.buttonsContainer?.Spacing ?? 0 : 0;
        this.MinWidth = MinDragWidth + marginLeft + pinWidth + closeWidth + spacing + marginRight;
        this.LogMinWidthUpdated();
    }

    /// <summary>
    ///     Updates the visibility of the toolbar based on compact mode and pointer state.
    /// </summary>
    private void UpdateToolBarVisibility()
        => this.buttonsContainer?.Visibility = this.IsCompact && !this.isPointerOver
            ? Visibility.Collapsed
            : Visibility.Visible;

    /// <summary>
    ///     Handles changes to the IsCompact property and updates toolbar visibility.
    /// </summary>
    /// <param name="oldValue">Previous value of IsCompact.</param>
    /// <param name="newValue">New value of IsCompact.</param>
    private void OnIsCompactChanged(bool oldValue, bool newValue)
    {
        this.LogCompactModeChanged(oldValue, newValue);
        this.UpdateToolBarVisibility();
    }
}
