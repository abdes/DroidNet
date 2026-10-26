// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace DroidNet.Aura.Dialogs;

/// <summary>
///     A <see cref="ContentDialog"/> whose button bar also holds footer content on its left and, beside the primary
///     button, a chevron that opens the primary button's flyout.
/// </summary>
/// <remarks>
///     The template is WinUI's <see cref="ContentDialog"/> template with a button bar of two columns: the footer, then
///     the dialog buttons. Footer content comes first in reading and tab order, and it never overlaps the buttons: it
///     takes the width the buttons leave. The chevron is a button of its own rather than a split button, so it stays
///     enabled while the primary button is disabled. It uses the primary button's style and the control corner radius
///     of the theme.
///     <para>
///     Template parts, in addition to those of <see cref="ContentDialog"/>:
///     </para>
///     <list type="bullet">
///         <item><description><c>"FooterPresenter"</c> — the <see cref="ContentPresenter"/> of <see cref="FooterContent"/>.</description></item>
///         <item><description><c>"PrimaryButtonFlyoutButton"</c> — the chevron <see cref="Button"/> that opens <see cref="PrimaryButtonFlyout"/>.</description></item>
///     </list>
///     <para>
///     Visual state groups: <c>FooterStates</c> (<c>NoFooter</c>, <c>FooterVisible</c>),
///     <c>PrimaryButtonFlyoutStates</c> (<c>NoPrimaryButtonFlyout</c>, <c>PrimaryButtonFlyoutVisible</c>) and
///     <c>PrimaryButtonFlyoutIndicatorStates</c> (<c>PrimaryButtonFlyoutIndicatorHidden</c>,
///     <c>PrimaryButtonFlyoutIndicatorVisible</c>).
///     </para>
/// </remarks>
[TemplatePart(Name = FooterPresenterPartName, Type = typeof(ContentPresenter))]
[TemplatePart(Name = PrimaryButtonFlyoutButtonPartName, Type = typeof(Button))]
[TemplateVisualState(GroupName = FooterStatesGroupName, Name = NoFooterStateName)]
[TemplateVisualState(GroupName = FooterStatesGroupName, Name = FooterVisibleStateName)]
[TemplateVisualState(GroupName = PrimaryButtonFlyoutStatesGroupName, Name = NoPrimaryButtonFlyoutStateName)]
[TemplateVisualState(GroupName = PrimaryButtonFlyoutStatesGroupName, Name = PrimaryButtonFlyoutVisibleStateName)]
[TemplateVisualState(GroupName = PrimaryButtonFlyoutIndicatorStatesGroupName, Name = PrimaryButtonFlyoutIndicatorHiddenStateName)]
[TemplateVisualState(GroupName = PrimaryButtonFlyoutIndicatorStatesGroupName, Name = PrimaryButtonFlyoutIndicatorVisibleStateName)]
public sealed partial class AuraDialog : ContentDialog
{
    /// <summary>The name of the footer presenter template part.</summary>
    public const string FooterPresenterPartName = "FooterPresenter";

    /// <summary>The name of the primary button's chevron template part.</summary>
    public const string PrimaryButtonFlyoutButtonPartName = "PrimaryButtonFlyoutButton";

    /// <summary>The name of the visual state group of the footer.</summary>
    public const string FooterStatesGroupName = "FooterStates";

    /// <summary>The name of the visual state without footer content.</summary>
    public const string NoFooterStateName = "NoFooter";

    /// <summary>The name of the visual state with footer content.</summary>
    public const string FooterVisibleStateName = "FooterVisible";

    /// <summary>The name of the visual state group of the primary button's chevron.</summary>
    public const string PrimaryButtonFlyoutStatesGroupName = "PrimaryButtonFlyoutStates";

    /// <summary>The name of the visual state without a primary button flyout.</summary>
    public const string NoPrimaryButtonFlyoutStateName = "NoPrimaryButtonFlyout";

    /// <summary>The name of the visual state with the primary button's chevron.</summary>
    public const string PrimaryButtonFlyoutVisibleStateName = "PrimaryButtonFlyoutVisible";

    /// <summary>The name of the visual state group of the chevron's dot.</summary>
    public const string PrimaryButtonFlyoutIndicatorStatesGroupName = "PrimaryButtonFlyoutIndicatorStates";

    /// <summary>The name of the visual state without the chevron's dot.</summary>
    public const string PrimaryButtonFlyoutIndicatorHiddenStateName = "PrimaryButtonFlyoutIndicatorHidden";

    /// <summary>The name of the visual state with the chevron's dot.</summary>
    public const string PrimaryButtonFlyoutIndicatorVisibleStateName = "PrimaryButtonFlyoutIndicatorVisible";

    /// <summary>Identifies the <see cref="FooterContent"/> dependency property.</summary>
    public static readonly DependencyProperty FooterContentProperty = DependencyProperty.Register(
        nameof(FooterContent),
        typeof(object),
        typeof(AuraDialog),
        new PropertyMetadata(defaultValue: null, OnVisualStatePropertyChanged));

    /// <summary>Identifies the <see cref="PrimaryButtonFlyout"/> dependency property.</summary>
    public static readonly DependencyProperty PrimaryButtonFlyoutProperty = DependencyProperty.Register(
        nameof(PrimaryButtonFlyout),
        typeof(FlyoutBase),
        typeof(AuraDialog),
        new PropertyMetadata(defaultValue: null, OnVisualStatePropertyChanged));

    /// <summary>Identifies the <see cref="IsPrimaryButtonFlyoutIndicatorVisible"/> dependency property.</summary>
    public static readonly DependencyProperty IsPrimaryButtonFlyoutIndicatorVisibleProperty = DependencyProperty.Register(
        nameof(IsPrimaryButtonFlyoutIndicatorVisible),
        typeof(bool),
        typeof(AuraDialog),
        new PropertyMetadata(defaultValue: false, OnVisualStatePropertyChanged));

    private Button? primaryButtonFlyoutButton;

    /// <summary>
    ///     Initializes a new instance of the <see cref="AuraDialog"/> class.
    /// </summary>
    public AuraDialog()
    {
        this.DefaultStyleKey = typeof(AuraDialog);
        _ = this.RegisterPropertyChangedCallback(PrimaryButtonTextProperty, (_, _) => this.UpdatePrimaryButtonFlyoutButtonName());
    }

    /// <summary>
    ///     Gets or sets the content shown on the left of the button bar; <see langword="null"/> for none.
    /// </summary>
    public object? FooterContent
    {
        get => this.GetValue(FooterContentProperty);
        set => this.SetValue(FooterContentProperty, value);
    }

    /// <summary>
    ///     Gets or sets the flyout that the chevron beside the primary button opens; <see langword="null"/> hides the
    ///     chevron.
    /// </summary>
    public FlyoutBase? PrimaryButtonFlyout
    {
        get => (FlyoutBase?)this.GetValue(PrimaryButtonFlyoutProperty);
        set => this.SetValue(PrimaryButtonFlyoutProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether the chevron of <see cref="PrimaryButtonFlyout"/> shows a dot, such as
    ///     when the flyout's settings differ from their defaults.
    /// </summary>
    public bool IsPrimaryButtonFlyoutIndicatorVisible
    {
        get => (bool)this.GetValue(IsPrimaryButtonFlyoutIndicatorVisibleProperty);
        set => this.SetValue(IsPrimaryButtonFlyoutIndicatorVisibleProperty, value);
    }

    /// <inheritdoc/>
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        this.primaryButtonFlyoutButton = this.GetTemplateChild(PrimaryButtonFlyoutButtonPartName) as Button;
        this.UpdatePrimaryButtonFlyoutButtonName();
        this.UpdateVisualStates(useTransitions: false);
    }

    private static void OnVisualStatePropertyChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((AuraDialog)sender).UpdateVisualStates(useTransitions: true);

    private void UpdateVisualStates(bool useTransitions)
    {
        _ = VisualStateManager.GoToState(
            this,
            this.FooterContent is null ? NoFooterStateName : FooterVisibleStateName,
            useTransitions);
        _ = VisualStateManager.GoToState(
            this,
            this.PrimaryButtonFlyout is null ? NoPrimaryButtonFlyoutStateName : PrimaryButtonFlyoutVisibleStateName,
            useTransitions);
        _ = VisualStateManager.GoToState(
            this,
            this.IsPrimaryButtonFlyoutIndicatorVisible ? PrimaryButtonFlyoutIndicatorVisibleStateName : PrimaryButtonFlyoutIndicatorHiddenStateName,
            useTransitions);
    }

    private void UpdatePrimaryButtonFlyoutButtonName()
    {
        if (this.primaryButtonFlyoutButton is not { } chevron)
        {
            return;
        }

        var name = string.Format(CultureInfo.CurrentCulture, "{0} options", this.PrimaryButtonText);
        AutomationProperties.SetName(chevron, name);
        ToolTipService.SetToolTip(chevron, name);
    }
}
