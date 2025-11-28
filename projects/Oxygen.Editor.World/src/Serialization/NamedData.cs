// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Base data transfer object for named entities.
/// </summary>
public record NamedData
{
    /// <summary>
    /// Gets or initializes the name of the entity.
    /// </summary>
    public required string Name { get; init; }
}
