// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;

/// <summary>
/// Represents a custom thumbnail control that extends the <see cref="ContentControl" /> class. This control is designed
/// to display content with a customizable template, provided by a <see cref="DataTemplateSelector" /> for maximum flexibility.
/// </summary>
/// <remarks>
/// The control has two visual states corresponding to when it has a <see cref="ContentControl.ContentTemplateSelector" />,
/// in which case it will use it to get the <see cref="ContentControl.ContentTemplate" /> to be used, or when it is
/// using the default one. When a custom template is used, it is made available in the <see cref="CustomTemplate" />
/// property.
/// </remarks>
/// <seealso cref="ContentControl.ContentTemplateSelector" />
/// <seealso cref="CustomTemplate" />
[TemplateVisualState(Name = DefaultTemplateVisualState, GroupName = TemplateVisualStates)]
[TemplateVisualState(Name = CustomTemplateVisualState, GroupName = TemplateVisualStates)]
[ContentProperty(Name = nameof(Content))]
public partial class Thumbnail : ContentControl
{
    private const string TemplateVisualStates = "TemplateStates";
    private const string CustomTemplateVisualState = "CustomTemplate";
    private const string DefaultTemplateVisualState = "DefaultTemplate";

    /// <summary>
    /// Initializes a new instance of the <see cref="Thumbnail" /> class.
    /// </summary>
    /// <remarks>
    /// This constructor sets the default style key for the <see cref="Thumbnail" /> control to ensure it uses the correct
    /// style defined in the application resources.
    /// </remarks>
    public Thumbnail() => this.DefaultStyleKey = typeof(Thumbnail);

    /// <summary>
    /// Called when the content of the <see cref="Thumbnail" /> control changes, in which case, it will update the
    /// visual state of the control based on the new content.
    /// </summary>
    /// <param name="oldContent">The old content of the control.</param>
    /// <param name="newContent">The new content of the control.</param>
    protected override void OnContentChanged(object oldContent, object newContent)
    {
        base.OnContentChanged(oldContent, newContent);
        this.UpdateTemplateState();
    }

    /// <summary>
    /// Called when the <see cref="ContentControl.ContentTemplateSelector" /> property changes, in which case, it
    /// updates the visual state of the <see cref="Thumbnail" /> control based on the new content template selector.
    /// </summary>
    /// <param name="oldContentTemplateSelector">The old <see cref="DataTemplateSelector" />.</param>
    /// <param name="newContentTemplateSelector">The new <see cref="DataTemplateSelector" />.</param>
    /// <remarks>
    /// This method updates the visual state of the <see cref="Thumbnail" /> control based on the new content template
    /// selector.
    /// </remarks>
    protected override void OnContentTemplateSelectorChanged(
        DataTemplateSelector oldContentTemplateSelector,
        DataTemplateSelector newContentTemplateSelector)
    {
        base.OnContentTemplateSelectorChanged(oldContentTemplateSelector, newContentTemplateSelector);
        this.UpdateTemplateState();
    }

    /// <summary>
    /// Updates the visual state of the <see cref="Thumbnail" /> control based on the presence of a
    /// <see cref="ContentControl.ContentTemplateSelector" />. If a custom template selector is provided, it selects the
    /// appropriate template and transitions to the custom template visual state. Otherwise, it transitions to the
    /// default template visual state, whihc uses the default template.
    /// </summary>
    private void UpdateTemplateState()
    {
        if (this.Content is null)
        {
            return;
        }

        if (this.ContentTemplateSelector != null)
        {
            this.CustomTemplate = this.ContentTemplateSelector.SelectTemplate(this.Content, this);
            VisualStateManager.GoToState(this, CustomTemplateVisualState, useTransitions: true);
        }
        else
        {
            VisualStateManager.GoToState(this, DefaultTemplateVisualState, useTransitions: true);
        }
    }
}
