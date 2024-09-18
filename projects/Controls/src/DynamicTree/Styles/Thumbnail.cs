// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.DynamicTree.Styles;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public class Thumbnail : ContentControl
{
    public Thumbnail() => this.DefaultStyleKey = typeof(Thumbnail);

    protected override void OnContentChanged(object oldContent, object newContent)
    {
        base.OnContentChanged(oldContent, newContent);
        this.UpdateTemplate();
    }

    protected override void OnContentTemplateSelectorChanged(
        DataTemplateSelector oldContentTemplateSelector,
        DataTemplateSelector newContentTemplateSelector)
    {
        base.OnContentTemplateSelectorChanged(oldContentTemplateSelector, newContentTemplateSelector);
        this.UpdateTemplate();
    }

    private void UpdateTemplate()
        => this.ContentTemplate =
            this.ContentTemplateSelector is not null
                ? this.ContentTemplateSelector.SelectTemplate(this.Content, this)
                : (DataTemplate)Application.Current.Resources["DefaultThumbnailTemplate"];
}
