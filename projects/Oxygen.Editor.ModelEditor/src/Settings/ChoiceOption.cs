// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>One choice of a setting row's combo box.</summary>
/// <param name="Value">The setting's value, or <see langword="null"/> for the choice that leaves the setting to the
/// file, such as its source size or its default scene.</param>
/// <param name="Label">The choice's text.</param>
public sealed record ChoiceOption(object? Value, string Label)
{
    /// <inheritdoc />
    public override string ToString() => this.Label;
}
