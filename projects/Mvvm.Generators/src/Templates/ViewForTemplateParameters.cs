// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Mvvm.Generators.Templates;

/// <summary>
/// Initializes a new instance of the <see cref="ViewForTemplateParameters" />
/// class, containing the values used in the substitutions inside the template.
/// </summary>
/// <param name="viewClassName">The class name of the view.</param>
/// <param name="viewModelClassName">The class name of the view model.</param>
/// <param name="viewNamespace">The namespace of the view.</param>
/// <param name="viewModelNamespace">The namespace of the view model.</param>
internal sealed class ViewForTemplateParameters(
    string viewClassName,
    string viewModelClassName,
    string viewNamespace,
    string viewModelNamespace)
{
    /// <summary>Gets the class name of the view.</summary>
    /// <value>The class name of the view.</value>
    public string ViewClassName { get; } = viewClassName;

    /// <summary>Gets the class name of the view model.</summary>
    /// <value>The class name of the view model.</value>
    public string ViewModelClassName { get; } = viewModelClassName;

    /// <summary>Gets the namespace of the view.</summary>
    /// <value>The namespace of the view.</value>
    public string ViewNamespace { get; } = viewNamespace;

    /// <summary>Gets the namespace of the view model.</summary>
    /// <value>The namespace of the view model.</value>
    public string ViewModelNamespace { get; } = viewModelNamespace;
}
