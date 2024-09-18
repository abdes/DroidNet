// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.DynamicTree;

using System.Reflection;
using Microsoft.UI.Xaml.Markup;

public sealed class ConstantExtension : MarkupExtension
{
    public string? TypeName { get; set; }

    public string? PropertyName { get; set; }

    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Usage",
        "MA0015:Specify the parameter name in ArgumentException",
        Justification = "properties serve as arguments")]
    protected override object? ProvideValue()
    {
        if (string.IsNullOrEmpty(this.TypeName) || string.IsNullOrEmpty(this.PropertyName))
        {
            throw new ArgumentException("TypeName and PropertyName must be set.");
        }

        var type = Type.GetType(this.TypeName) ?? throw new ArgumentException($"Type '{this.TypeName}' not found.");

        var field = type.GetField(this.PropertyName, BindingFlags.Public | BindingFlags.Static);
        if (field != null)
        {
            return field.GetValue(null);
        }

        var property = type.GetProperty(this.PropertyName, BindingFlags.Public | BindingFlags.Static);
        if (property != null)
        {
            return property.GetValue(null);
        }

        throw new ArgumentException($"Property or field '{this.PropertyName}' not found on type '{this.TypeName}'.");
    }
}
