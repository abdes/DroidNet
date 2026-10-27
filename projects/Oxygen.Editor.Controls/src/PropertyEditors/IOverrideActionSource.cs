// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Controls.PropertyEditors;

/// <summary>
/// Supplies the commands an overridden row offers besides reset, such as applying its value to the source it overrides:
/// <see cref="PropertyCard"/> asks for them when its context menu opens.
/// </summary>
public interface IOverrideActionSource
{
    /// <summary>Gets the commands the row offers now.</summary>
    /// <returns>The commands, in menu order; empty when there are none.</returns>
    public Task<IReadOnlyList<OverrideAction>> GetActionsAsync();
}
