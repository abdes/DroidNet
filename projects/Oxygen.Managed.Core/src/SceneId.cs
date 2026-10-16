// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Managed.Core;

/// <summary>
///     Identifies a scene by its content: it is authored in the scene file, so it follows the scene wherever its file is,
///     and a copy of the scene gets a new one.
/// </summary>
/// <param name="Value">The underlying value.</param>
[JsonConverter(typeof(GuidIdJsonConverter<SceneId>))]
public readonly record struct SceneId(Guid Value) : IGuidId<SceneId>
{
    /// <summary>
    ///     Gets the id that identifies no scene.
    /// </summary>
    public static SceneId Empty { get; }

    /// <summary>
    ///     Gets a value indicating whether this id identifies no scene.
    /// </summary>
    public bool IsEmpty => this.Value == Guid.Empty;

    /// <summary>
    ///     Creates a new, unique scene id.
    /// </summary>
    /// <returns>The new id.</returns>
    public static SceneId New() => new(Guid.NewGuid());

    /// <inheritdoc/>
    public static SceneId FromValue(Guid value) => new(value);

    /// <inheritdoc/>
    public override string ToString() => this.Value.ToString("D");
}
