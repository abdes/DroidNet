// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ProjectExplorer;

using DroidNet.Controls;
using Microsoft.UI.Xaml.Controls;
using DataTemplate = Microsoft.UI.Xaml.DataTemplate;
using DependencyObject = Microsoft.UI.Xaml.DependencyObject;

/// <summary>
/// A <see cref="DataTemplateSelector" /> that can map a <see cref="TreeItemAdapter" /> to a template that can be used to display a
/// <see cref="Thumbnail" /> for it.
/// </summary>
public partial class ThumbnailTemplateSelector : DataTemplateSelector
{
#if false
    public ThumbnailTemplateSelector()
    {
        const string xaml = """
                            <DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'>
                                <Border
                                    Width='{StaticResource CellContentWidth}'
                                    Height='{StaticResource CellContentHeight}'
                                    BorderBrush='{ThemeResource EmptyThumbnailBorderColor}'
                                    BorderThickness='1' />
                            </DataTemplate>
                            """;

        this.DefaultTemplate = (DataTemplate)XamlReader.Load(xaml);
    }
#endif

    public DataTemplate? SceneTemplate { get; set; }

    public DataTemplate? EntityTemplate { get; set; }

    public DataTemplate? DefaultTemplate { get; set; }

    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container)
        => item switch
        {
            SceneAdapter => this.SceneTemplate,
            EntityAdapter => this.EntityTemplate,
            _ => this.DefaultTemplate,
        };
}
