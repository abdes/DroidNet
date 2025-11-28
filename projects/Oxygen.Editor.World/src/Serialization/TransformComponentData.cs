// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for Transform component.
/// </summary>
public record TransformComponentData : ComponentData
{
    /// <summary>
    /// Gets or initializes the transform data.
    /// </summary>
    public required TransformData Transform { get; init; }
}
