// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.Model;

/// <summary>
/// Represents an entity with a name.
/// </summary>
/// <param name="name">The name of the entity.</param>
public class Entity(string name) : NamedItem(name);
