// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;

namespace DroidNet.Controls;

/// <summary>
/// Represents a custom thumbnail control that extends the <see cref="ContentControl"/> class.
/// This control is designed to display content with a customizable template, provided by a
/// <see cref="DataTemplateSelector"/> for maximum flexibility.
/// </summary>
/// <remarks>
/// The <see cref="Thumbnail"/> control follows the behavior of <see cref="ContentControl"/> in giving precedence to
/// <see cref="ContentControl.ContentTemplate"/> when it is set. If <see cref="ContentControl.ContentTemplate"/> is not set,
/// but <see cref="ContentControl.ContentTemplateSelector"/> is set, the control will use the template selected by the
/// <see cref="ContentControl.ContentTemplateSelector"/>. If neither is set, after the control is fully initialized, it will use a
/// default template defined in the application resources.
/// <para>
/// <strong>Important:</strong> Ensure that the default template is defined in the application resources with the key
/// "DefaultThumbnailTemplate". This can be done by merging the resource dictionary containing the default template into the
/// control library's <see langword="Themes/Generic.xaml"/> file.
/// </para>
/// </remarks>
/// <example>
/// <para><strong>Example: Using the default template</strong></para>
/// <![CDATA[
/// <local:Thumbnail Content="Sample Content" />
/// ]]>
/// </example>
/// <example>
/// <para><strong>Example: Using a custom template</strong></para>
/// <![CDATA[
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
///
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
/// <seealso cref="ContentControl.ContentTemplate"/>
/// <seealso cref="ContentControl.ContentTemplateSelector"/>
[ContentProperty(Name = nameof(Content))]
public partial class Thumbnail : ContentControl
{
    private bool isInitialized;

    /// <summary>
    /// Initializes a new instance of the <see cref="Thumbnail" /> class.
    /// </summary>
    /// <remarks>
    /// This constructor sets the default style key for the <see cref="Thumbnail" /> control to
    /// ensure it uses the correct style defined in the application resources.
    /// </remarks>
    public Thumbnail()
    {
        this.DefaultStyleKey = typeof(Thumbnail);
        this.Loaded += this.OnLoaded;

        // Merge the resource dictionary containing the DefaultThumbnailTemplate
        var assemblyName = Assembly.GetExecutingAssembly().GetName().Name;
        var resourceDictionary = new ResourceDictionary
        {
            Source = new Uri($"ms-appx:///{assemblyName}/Thumbnail/Thumbnail.xaml"),
        };
        this.Resources.MergedDictionaries.Add(resourceDictionary);
    }

    /// <inheritdoc/>
    /// <remarks>
    /// If the control is already loaded, when this property changes and neither the <see cref="ContentControl.ContentTemplate"/> nor
    /// the <see cref="ContentControl.ContentTemplateSelector"/> is set, the default template will be used.
    /// </remarks>
    protected override void OnContentTemplateChanged(DataTemplate oldContentTemplate, DataTemplate newContentTemplate)
    {
        base.OnContentTemplateChanged(oldContentTemplate, newContentTemplate);
        this.UpdateContentTemplate();
    }

    /// <inheritdoc/>
    /// <remarks>
    /// If the control is already loaded, when this property changes and neither the <see cref="ContentControl.ContentTemplate"/> nor
    /// the <see cref="ContentControl.ContentTemplateSelector"/> is set, the default template will be used.
    /// </remarks>
    protected override void OnContentTemplateSelectorChanged(DataTemplateSelector oldContentTemplateSelector, DataTemplateSelector newContentTemplateSelector)
    {
        base.OnContentTemplateSelectorChanged(oldContentTemplateSelector, newContentTemplateSelector);
        this.UpdateContentTemplate();
    }

    /// <summary>
    /// Handles the <see cref="FrameworkElement.Loaded"/> event of the <see cref="Thumbnail"/> control.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    /// <remarks>
    /// This method sets the <see cref="isInitialized"/> flag to <see langword="true"/> and calls <see cref="UpdateContentTemplate"/>.
    /// If neither the <see cref="ContentControl.ContentTemplate"/> nor the <see cref="ContentControl.ContentTemplateSelector"/>
    /// is set, the control will use a default template defined in the application resources.
    /// </remarks>
    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        this.isInitialized = true;
        this.UpdateContentTemplate();
    }

    /// <summary>
    /// Updates the content template of the control based on the current properties.
    /// </summary>
    /// <remarks>
    /// This method ensures that the <see cref="ContentControl.ContentTemplate"/> property is set to the appropriate template
    /// based on the current properties of the control. If <see cref="ContentControl.ContentTemplate"/> is set, it takes
    /// precedence. If <see cref="ContentControl.ContentTemplate"/> is not set but <see cref="ContentControl.ContentTemplateSelector"/>
    /// is set, the control will use the template selected by the <see cref="ContentControl.ContentTemplateSelector"/>.
    /// <para>
    /// If neither is set, the control will use a default template defined in the application resources.
    /// </para>
    /// </remarks>
    private void UpdateContentTemplate()
    {
        if (!this.isInitialized)
        {
            return;
        }

        if (this.ContentTemplate is null)
        {
            if (this.ContentTemplateSelector is not null)
            {
                this.ContentTemplate = this.ContentTemplateSelector.SelectTemplate(this.Content, this);
            }

            this.ContentTemplate ??= (DataTemplate)this.Resources["DefaultThumbnailTemplate"];
        }
    }
}
