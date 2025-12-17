// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Inspector;

/// <summary>
///     Represents a section in a details or properties editor, typically used for displaying grouped information with a header, description, and expand/collapse state.
/// </summary>
public interface IDetailsSection
{
    /// <summary>
    ///     Gets the header text for the section.
    /// </summary>
    public string Header { get; }

    /// <summary>
    ///     Gets the description text for the section, providing additional context or information.
    /// </summary>
    public string Description { get; }

    /// <summary>
    ///     Gets a value indicating whether the section is currently expanded in the UI.
    /// </summary>
    public bool IsExpanded { get; }
}
