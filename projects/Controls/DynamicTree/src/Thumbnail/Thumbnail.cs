// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;

namespace DroidNet.Controls;

/// <summary>
///     Represents a custom thumbnail control that extends the <see cref="ContentControl" /> class.
///     This control is designed to display content with a customizable template, provided by a
///     <see cref="DataTemplateSelector" /> for maximum flexibility.
/// </summary>
/// <remarks>
///     The <see cref="Thumbnail" /> control follows the behavior of <see cref="ContentControl" />
///     in giving precedence to <see cref="ContentControl.ContentTemplate" /> when it is set. If
///     <see cref="ContentControl.ContentTemplate" /> is not set, but <see cref="ContentControl.ContentTemplateSelector" />
///     is set, the control will use the template selected by the <see cref="ContentControl.ContentTemplateSelector" />.
///     If neither is set, it will use the library's default template (the "DefaultThumbnailTemplate"
///     resource of <c>Thumbnail.xaml</c>, loaded once per UI thread and shared by its instances).
///     <para>
///     A template assigned from outside (in XAML or code) is never replaced by the control. Otherwise,
///     the template is selected again every time the <see cref="ContentControl.Content" /> or the
///     <see cref="ContentControl.ContentTemplateSelector" /> changes, so a recycled thumbnail always
///     shows the template that matches its current content.</para>
/// </remarks>
/// <example>
/// <para><strong>Example: Using the default template</strong></para>
/// <![CDATA[
/// <local:Thumbnail Content="Sample Content" />
/// ]]>
/// </example>
/// <example>
///     <para>
///         <strong>Example: Using a custom template</strong>
///     </para>
///     <![CDATA[
/// <local:Thumbnail>
///     <local:Thumbnail.ContentTemplate>
///         <DataTemplate>
///             <StackPanel>
///                 <SymbolIcon Symbol="{Binding}" />
///                 <TextBlock Text="{Binding}" />
///             </StackPanel>
///         </DataTemplate>
///     </local:Thumbnail.ContentTemplate>
///     <local:Thumbnail.Content>
///         Camera
///     </local:Thumbnail.Content>
/// </local:Thumbnail>
/// ]]>
/// </example>
/// <example>
/// <para><strong>Example: Using a custom template selector</strong></para>
/// <![CDATA[
/// <local:Thumbnail Content="Sample Content" ContentTemplateSelector="{StaticResource CustomTemplateSelector}" />
/// ]]>
/// <para><strong>Custom Template Selector Implementation:</strong></para>
/// <![CDATA[
/// public class CustomTemplateSelector : DataTemplateSelector
/// {
///     public DataTemplate CameraTemplate { get; set; }
///     public DataTemplate DefaultTemplate { get; set; }
///     protected override DataTemplate SelectTemplateCore(object item, DependencyObject container)
///     {
///         if (item is string content && content == "Camera")
///         {
///             return CameraTemplate;
///         }
///         return DefaultTemplate;
///     }
/// }
/// ]]>
/// <para><strong>XAML Definition for CustomTemplateSelector:</strong></para>
/// <![CDATA[
/// <local:CustomTemplateSelector x:Key="CustomTemplateSelector">
///     <local:CustomTemplateSelector.CameraTemplate>
///         <DataTemplate>
///             <StackPanel>
///                 <SymbolIcon Symbol="Camera" />
///                 <TextBlock Text="Camera" />
///             </StackPanel>
///         </DataTemplate>
///     </local:CustomTemplateSelector.CameraTemplate>
///     <local:CustomTemplateSelector.DefaultTemplate>
///         <DataTemplate>
///             <StackPanel>
///                 <SymbolIcon Symbol="Help" />
///                 <TextBlock Text="Default" />
///             </StackPanel>
///         </DataTemplate>
///     </local:CustomTemplateSelector.DefaultTemplate>
/// </local:CustomTemplateSelector>
/// ]]>
/// </example>
/// <seealso cref="ContentControl.ContentTemplate" />
/// <seealso cref="ContentControl.ContentTemplateSelector" />
[ContentProperty(Name = nameof(Content))]
public partial class Thumbnail : ContentControl
{
    // A XAML object belongs to the thread that created it, so each UI thread loads its own copy. A failed load is not
    // remembered: the next thumbnail tries again.
    private static readonly ThreadLocal<DataTemplate> DefaultTemplate = new(LoadDefaultTemplate);

    private bool isTemplateSetExternally;
    private bool isUpdatingTemplate;

    /// <summary>
    ///     Initializes a new instance of the <see cref="Thumbnail" /> class.
    /// </summary>
    /// <remarks>
    ///     This constructor sets the default style key for the <see cref="Thumbnail" /> control to
    ///     ensure it uses the correct style defined in the application resources, and selects the
    ///     initial content template.
    /// </remarks>
    public Thumbnail()
    {
        this.DefaultStyleKey = typeof(Thumbnail);
        this.UpdateContentTemplate();
    }

    /// <inheritdoc />
    protected override void OnContentChanged(object oldContent, object newContent)
    {
        base.OnContentChanged(oldContent, newContent);
        this.UpdateContentTemplate();
    }

    /// <inheritdoc />
    /// <remarks>
    ///     A non-<see langword="null" /> template assigned from outside takes precedence over the
    ///     selector. When it is cleared, the template is selected again.
    /// </remarks>
    protected override void OnContentTemplateChanged(DataTemplate oldContentTemplate, DataTemplate newContentTemplate)
    {
        base.OnContentTemplateChanged(oldContentTemplate, newContentTemplate);

        if (this.isUpdatingTemplate)
        {
            return;
        }

        this.isTemplateSetExternally = newContentTemplate is not null;
        this.UpdateContentTemplate();
    }

    /// <inheritdoc />
    /// <remarks>
    ///     Unless a template was assigned from outside, the template is selected again with the new
    ///     selector, or the default template is used when the selector is cleared.
    /// </remarks>
    protected override void OnContentTemplateSelectorChanged(
        DataTemplateSelector oldContentTemplateSelector,
        DataTemplateSelector newContentTemplateSelector)
    {
        base.OnContentTemplateSelectorChanged(oldContentTemplateSelector, newContentTemplateSelector);
        this.UpdateContentTemplate();
    }

    /// <summary>
    ///     Loads the default template from <c>Thumbnail.xaml</c>.
    /// </summary>
    /// <remarks>
    ///     The dictionary merges shared resources and is expensive to build, so this runs once per UI thread
    ///     instead of once per realized row.
    /// </remarks>
    /// <returns>The shared default <see cref="DataTemplate" />.</returns>
    private static DataTemplate LoadDefaultTemplate()
    {
        var assemblyName = Assembly.GetExecutingAssembly().GetName().Name;
        var resources = new ResourceDictionary
        {
            Source = new Uri($"ms-appx:///{assemblyName}/Thumbnail/Thumbnail.xaml"),
        };
        return (DataTemplate)resources["DefaultThumbnailTemplate"];
    }

    /// <summary>
    ///     Selects the content template from the current content and selector, unless a template was
    ///     assigned from outside, in which case that template is kept.
    /// </summary>
    /// <remarks>
    ///     The template comes from the <see cref="ContentControl.ContentTemplateSelector" /> when it is
    ///     set and returns one, and from the default template otherwise.
    /// </remarks>
    private void UpdateContentTemplate()
    {
        if (this.isTemplateSetExternally)
        {
            return;
        }

        var selectedTemplate = this.ContentTemplateSelector?.SelectTemplate(this.Content, this) ?? DefaultTemplate.Value;
        if (ReferenceEquals(this.ContentTemplate, selectedTemplate))
        {
            return;
        }

        this.isUpdatingTemplate = true;
        try
        {
            this.ContentTemplate = selectedTemplate;
        }
        finally
        {
            this.isUpdatingTemplate = false;
        }
    }
}
