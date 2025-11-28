// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World;

/// <summary>
/// Represents an object that has a name.
/// </summary>
public interface INamed
{
    /// <summary>
    /// Gets or sets the name of the object.
    /// </summary>
    public string Name { get; set; }
}
