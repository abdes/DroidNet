// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls;

/// <summary>
/// Represents a label that can be edited in place. This control allows users to double-click on the label to switch to an edit mode,
/// where they can modify the text. The control provides visual states for normal, editing, and invalid value scenarios.
/// </summary>
/// <remarks>
/// The <see cref="InPlaceEditableLabel"/> control is useful in scenarios where inline editing of text is required. It supports validation
/// through the <see cref="Validate"/> event, which allows implementors to provide custom validation logic.
/// </remarks>
/// <example>
/// <para><strong>Example Usage</strong></para>
/// <code><![CDATA[
/// <controls:InPlaceEditableLabel Text="Sample Text" Validate="OnValidate">
///     <TextBlock FontSize="18" />
/// </controls:InPlaceEditableLabel>
/// ]]></code>
/// </example>
[TemplatePart(Name = RootGridPartName, Type = typeof(Grid))]
[TemplatePart(Name = LabelContentPresenterPartName, Type = typeof(ContentPresenter))]
[TemplatePart(Name = EditBoxPartName, Type = typeof(TextBox))]
[TemplatePart(Name = ValueErrorPartName, Type = typeof(FontIcon))]
[TemplateVisualState(Name = NormalStateName, GroupName = EditingVisualStatesGroupName)]
[TemplateVisualState(Name = EditingStateName, GroupName = EditingVisualStatesGroupName)]
[TemplateVisualState(Name = InvalidValueStateName, GroupName = EditingVisualStatesGroupName)]
public partial class InPlaceEditableLabel : ContentControl
{
    /// <summary>
    /// The name of the root grid part in the control template.
    /// </summary>
    public const string RootGridPartName = "PartRootGrid";

    /// <summary>
    /// The name of the content presenter part in the control template.
    /// </summary>
    public const string LabelContentPresenterPartName = "PartContentPresenter";

    /// <summary>
    /// The name of the edit box part in the control template.
    /// </summary>
    public const string EditBoxPartName = "PartEditBox";

    /// <summary>
    /// The name of the value error part in the control template.
    /// </summary>
    public const string ValueErrorPartName = "PartValueError";

    /// <summary>
    /// The name of the visual states group for editing states.
    /// </summary>
    public const string EditingVisualStatesGroupName = "EditingVisualStates";

    /// <summary>
    /// The name of the normal visual state.
    /// </summary>
    public const string NormalStateName = "Normal";

    /// <summary>
    /// The name of the editing visual state.
    /// </summary>
    public const string EditingStateName = "Editing";

    /// <summary>
    /// The name of the invalid value visual state.
    /// </summary>
    public const string InvalidValueStateName = "InvalidValue";

    /// <summary>
    /// Identifies the <see cref="Text"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty TextProperty =
        DependencyProperty.Register(
            nameof(Text),
            typeof(string),
            typeof(InPlaceEditableLabel),
            new PropertyMetadata(
                defaultValue: null,
                (d, _) => ((InPlaceEditableLabel)d).UpdateDisplayText()));

    /// <summary>
    /// Identifies the <see cref="DisplayText"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty DisplayTextProperty =
        DependencyProperty.Register(
            nameof(DisplayText),
            typeof(string),
            typeof(InPlaceEditableLabel),
            new PropertyMetadata(default));

    private bool isEditing;
    private TextBox? editTextBox;
    private TextBlock? entityNameTextBlock;
    private string? originalName;
    private bool newNameIsValid;

    /// <summary>
    /// Initializes a new instance of the <see cref="InPlaceEditableLabel"/> class.
    /// </summary>
    public InPlaceEditableLabel()
    {
        this.DefaultStyleKey = typeof(InPlaceEditableLabel);
    }

    /// <summary>
    /// Occurs when the text is being validated.
    /// </summary>
    public event EventHandler<ValidationEventArgs>? Validate;

    /// <summary>
    /// Gets or sets the text of the label. This is a dependency property.
    /// </summary>
    /// <value>The text of the label.</value>
    public string? Text
    {
        get => (string?)this.GetValue(TextProperty);
        set => this.SetValue(TextProperty, value);
    }

    /// <summary>
    /// Gets or sets the display text of the label. This is a dependency property.
    /// </summary>
    /// <value>The display text of the label.</value>
    public string DisplayText
    {
        get => (string)this.GetValue(DisplayTextProperty);
        set => this.SetValue(DisplayTextProperty, value);
    }

    /// <summary>
    /// Starts the edit mode, allowing the user to modify the text.
    /// </summary>
    internal void StartEdit()
    {
        if (this.entityNameTextBlock is null || this.editTextBox is null)
        {
            return;
        }

        this.originalName = this.Text;
        this.ValidateName();
        this.isEditing = true;

        this.UpdateVisualState();

        this.editTextBox.MinWidth = this.entityNameTextBlock.ActualWidth;
        _ = this.editTextBox.Focus(FocusState.Programmatic);
    }

    /// <summary>
    /// Commits the edit and exits the edit mode.
    /// </summary>
    internal void CommitEdit() => this.EndEdit();

    /// <summary>
    /// Cancels the edit and reverts to the original text.
    /// </summary>
    internal void CancelEdit()
    {
        if (this.editTextBox is not null)
        {
            this.editTextBox.Text = this.originalName;
        }

        this.EndEdit();
    }

    /// <summary>
    /// Handles the text changed event and validates the new text.
    /// </summary>
    internal void OnTextChanged()
    {
        this.ValidateName();
        this.UpdateVisualState(useTransitions: false);
    }

    /// <summary>
    /// Handles the lost focus event in the edit box.
    /// </summary>
    internal void OnEditBoxLostFocus()
    {
        if (this.newNameIsValid)
        {
            this.CommitEdit();
        }
        else
        {
            this.CancelEdit();
        }
    }

    /// <summary>
    /// Handles the got focus event in the edit box.
    /// </summary>
    internal void OnEditBoxGotFocus() => this.editTextBox?.SelectAll();

    /// <inheritdoc/>
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        this.editTextBox = this.GetTemplateChild(EditBoxPartName) as TextBox;

        var contentPresenter = this.GetTemplateChild(LabelContentPresenterPartName) as ContentPresenter;
        if (contentPresenter is not null)
        {
            contentPresenter.Loaded += (_, _) =>
            {
                this.entityNameTextBlock = FindVisualChild<TextBlock>(contentPresenter);
                if (this.entityNameTextBlock is not null)
                {
                    this.entityNameTextBlock.SetBinding(TextBlock.TextProperty, new Binding
                    {
                        Source = this,
                        Path = new PropertyPath(nameof(this.DisplayText)),
                        Mode = BindingMode.OneWay,
                    });
                    this.entityNameTextBlock.DoubleTapped += this.EntityNameTextBlock_DoubleTapped;
                }
            };
        }

        if (this.editTextBox is not null)
        {
            // Register event handlers once
            this.editTextBox.GotFocus += (_, _) => this.OnEditBoxGotFocus();
            this.editTextBox.KeyDown += this.OnEditBoxKeyDown;
            this.editTextBox.LostFocus += (_, _) => this.OnEditBoxLostFocus();
            this.editTextBox.TextChanged += (_, _) => this.OnTextChanged();
        }

        this.UpdateDisplayText();
        this.UpdateVisualState();
    }

    /// <summary>
    /// Finds a visual child of a specified type.
    /// </summary>
    /// <typeparam name="T">The type of the visual child to find.</typeparam>
    /// <param name="parent">The parent dependency object.</param>
    /// <returns>The found visual child, or <c>null</c> if no child is found.</returns>
    private static T? FindVisualChild<T>(DependencyObject parent)
        where T : DependencyObject
    {
        for (var i = 0; i < VisualTreeHelper.GetChildrenCount(parent); i++)
        {
            var child = VisualTreeHelper.GetChild(parent, i);
            if (child is T t)
            {
                return t;
            }

            var result = FindVisualChild<T>(child);
            if (result != null)
            {
                return result;
            }
        }

        return null;
    }

    /// <summary>
    /// Updates the display text based on the current text.
    /// </summary>
    private void UpdateDisplayText()
    {
        // Update the value of the DisplayText dependency property
        var displayText = string.IsNullOrEmpty(this.Text) ? "Multiple Values" : this.Text!;
        this.SetValue(DisplayTextProperty, displayText);
    }

    /// <summary>
    /// Updates the visual state of the control.
    /// </summary>
    /// <param name="useTransitions">Indicates whether to use transitions.</param>
    private void UpdateVisualState(bool useTransitions = true)
        => _ = this.isEditing
            ? this.newNameIsValid
                ? VisualStateManager.GoToState(this, EditingStateName, useTransitions)
                : VisualStateManager.GoToState(this, InvalidValueStateName, useTransitions)
            : VisualStateManager.GoToState(this, NormalStateName, useTransitions);

    /// <summary>
    /// Handles the double-tap event on the entity name text block to start editing.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="e">The event data.</param>
    private void EntityNameTextBlock_DoubleTapped(object sender, DoubleTappedRoutedEventArgs e)
    {
        e.Handled = true;
        this.StartEdit();
    }

    /// <summary>
    /// Ends the edit mode and updates the visual state.
    /// </summary>
    private void EndEdit()
    {
        this.isEditing = false;
        this.UpdateVisualState();
    }

    /// <summary>
    /// Validates the current text in the edit box.
    /// </summary>
    private void ValidateName()
    {
        if (this.editTextBox is null)
        {
            return;
        }

        var args = new ValidationEventArgs(this.editTextBox.Text);
        this.Validate?.Invoke(this, args);
        this.newNameIsValid = args.IsValid;
    }

    /// <summary>
    /// Handles the key down event in the edit box.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="e">The event data.</param>
    [SuppressMessage("ReSharper", "SwitchStatementMissingSomeEnumCasesNoDefault", Justification = "we only want to handle specific keys")]
    private void OnEditBoxKeyDown(object sender, KeyRoutedEventArgs e)
    {
        switch (e.Key)
        {
            case Windows.System.VirtualKey.Enter when !this.newNameIsValid:
                return;
            case Windows.System.VirtualKey.Enter:
                this.CommitEdit();
                e.Handled = true;
                break;

            case Windows.System.VirtualKey.Escape:
                this.CancelEdit();
                e.Handled = true;
                break;
        }
    }
}
