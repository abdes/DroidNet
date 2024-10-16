// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

using System.Text.Json.Serialization;

public class GameComponent(GameEntity entity) : NamedItem
{
    [JsonIgnore]
    public GameEntity Entity { get; } = entity;
}
