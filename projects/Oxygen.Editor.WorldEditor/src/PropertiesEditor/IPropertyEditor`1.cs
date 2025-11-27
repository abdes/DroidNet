// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

/// <summary>
///     Defines a contract for editing properties of one or more <see cref="GameObject"/> instances in a properties editor UI.
/// </summary>
/// <typeparam name="T">The type of game object being edited. Must inherit from <see cref="GameObject"/>.</typeparam>
public interface IPropertyEditor<T>
    where T : GameObject
{
    /// <summary>
    ///     Updates the editor's values based on the provided collection of items.
    ///     Typically called when the selection changes or when properties need to be refreshed in the UI.
    /// </summary>
    /// <param name="items">The collection of game objects whose properties are to be edited.</param>
    public void UpdateValues(ICollection<T> items);
}
