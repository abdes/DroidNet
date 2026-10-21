// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.SceneInspector.Components.Scripting;

/// <summary>Picks the editor template of a script parameter field from its kind.</summary>
public sealed partial class ScriptFieldTemplateSelector : DataTemplateSelector
{
    /// <summary>Gets or sets the boolean template.</summary>
    public DataTemplate? Toggle { get; set; }

    /// <summary>Gets or sets the number template.</summary>
    public DataTemplate? Number { get; set; }

    /// <summary>Gets or sets the option list template.</summary>
    public DataTemplate? Choice { get; set; }

    /// <summary>Gets or sets the text template.</summary>
    public DataTemplate? Text { get; set; }

    /// <summary>Gets or sets the vector template.</summary>
    public DataTemplate? Vector { get; set; }

    /// <summary>Gets or sets the color template.</summary>
    public DataTemplate? Color { get; set; }

    /// <summary>Gets or sets the asset template.</summary>
    public DataTemplate? Asset { get; set; }

    /// <summary>Gets or sets the node template.</summary>
    public DataTemplate? Node { get; set; }

    /// <inheritdoc />
    protected override DataTemplate? SelectTemplateCore(object item)
        => item is not ScriptParameterFieldViewModel field ? null : field.Kind switch
        {
            ScriptFieldKind.Toggle => this.Toggle,
            ScriptFieldKind.Number => this.Number,
            ScriptFieldKind.Choice => this.Choice,
            ScriptFieldKind.Text => this.Text,
            ScriptFieldKind.Vector => this.Vector,
            ScriptFieldKind.Color => this.Color,
            ScriptFieldKind.Asset => this.Asset,
            _ => this.Node,
        };

    /// <inheritdoc />
    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container) => this.SelectTemplateCore(item);
}
