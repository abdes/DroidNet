// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

[TemplatePart(Name = RootGridPartName, Type = typeof(Grid))]
[TemplatePart(Name = ActionIconPresenterHolderPartName, Type = typeof(Viewbox))]
[TemplatePart(Name = HeaderPresenterPartName, Type = typeof(ContentPresenter))]
[TemplatePart(Name = DescriptionPresenterPartName, Type = typeof(ContentPresenter))]
[TemplatePart(Name = HeaderIconPresenterHolderPartName, Type = typeof(Viewbox))]

[TemplateVisualState(Name = RightState, GroupName = ContentAlignmentStates)]
[TemplateVisualState(Name = RightWrappedState, GroupName = ContentAlignmentStates)]
[TemplateVisualState(Name = RightWrappedNoIconState, GroupName = ContentAlignmentStates)]

[TemplateVisualState(Name = VisibleState, GroupName = ContentVisibilityStates)]
[TemplateVisualState(Name = CollapsedState, GroupName = ContentVisibilityStates)]

[TemplateVisualState(Name = NormalState, GroupName = CommonStates)]
[TemplateVisualState(Name = PointerOverState, GroupName = CommonStates)]
[TemplateVisualState(Name = DisabledState, GroupName = CommonStates)]
public partial class PropertiesExpander : Control
{
    private const string CommonStates = "CommonStates";
    private const string NormalState = "Normal";
    private const string PointerOverState = "PointerOver";
    private const string DisabledState = "Disabled";

    private const string ContentVisibilityStates = "ContentVisibilityStates";
    private const string VisibleState = "Visible";
    private const string CollapsedState = "Collapsed";

    private const string ContentAlignmentStates = "ContentAlignmentStates";
    private const string RightState = "Right";
    private const string RightWrappedState = "RightWrapped";
    private const string RightWrappedNoIconState = "RightWrappedNoIcon";

    private const string ActionIconPresenterHolderPartName = "PartActionIconPresenterHolder";
    private const string RootGridPartName = "PartRootGrid";
    private const string HeaderPresenterPartName = "PartHeaderPresenter";
    private const string DescriptionPresenterPartName = "PartDescriptionPresenter";
    private const string HeaderIconPresenterHolderPartName = "PartHeaderIconPresenterHolder";

    /// <summary>
    /// Initializes a new instance of the <see cref="PropertiesExpander"/> class.
    /// </summary>
    public PropertiesExpander()
    {
        this.DefaultStyleKey = typeof(PropertiesExpander);

        this.Items = [];
    }

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        this.IsEnabledChanged -= this.OnIsEnabledChanged;

        this.OnHeaderChanged();
        this.OnHeaderIconChanged();
        this.OnDescriptionChanged();

        this.CheckInitialVisualState();

        this.PointerEntered += this.ControlPointerEntered;
        this.PointerExited += this.ControlPointerExited;

        this.IsEnabledChanged += this.OnIsEnabledChanged;
    }

    private static bool IsNullOrEmptyString(object? obj)
        => obj == null || (obj is string objString && string.IsNullOrEmpty(objString));

    private void CheckInitialVisualState()
        => VisualStateManager.GoToState(this, this.IsEnabled ? NormalState : DisabledState, useTransitions: true);

    private void ControlPointerEntered(object sender, PointerRoutedEventArgs e)
    {
        this.OnPointerEntered(e);
        _ = VisualStateManager.GoToState(this, PointerOverState, useTransitions: true);
    }

    private void ControlPointerExited(object sender, PointerRoutedEventArgs e)
    {
        this.OnPointerExited(e);
        _ = VisualStateManager.GoToState(this, NormalState, useTransitions: true);
    }

    private void OnIsEnabledChanged(object sender, DependencyPropertyChangedEventArgs e)
        => VisualStateManager.GoToState(this, this.IsEnabled ? NormalState : DisabledState, useTransitions: true);

    private void OnHeaderIconChanged()
    {
        if (this.GetTemplateChild(HeaderIconPresenterHolderPartName) is FrameworkElement headerIconPresenter)
        {
            headerIconPresenter.Visibility = this.HeaderIcon != null ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    private void OnDescriptionChanged()
    {
        if (this.GetTemplateChild(DescriptionPresenterPartName) is FrameworkElement descriptionPresenter)
        {
            descriptionPresenter.Visibility = IsNullOrEmptyString(this.Description) ? Visibility.Collapsed : Visibility.Visible;
        }
    }

    private void OnHeaderChanged()
    {
        if (this.GetTemplateChild(HeaderPresenterPartName) is FrameworkElement headerPresenter)
        {
            headerPresenter.Visibility = IsNullOrEmptyString(this.Header) ? Visibility.Collapsed : Visibility.Visible;
        }
    }
}
