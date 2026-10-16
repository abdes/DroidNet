// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World;

/// <summary>
///     Identifies a folder of a scene's explorer layout. Folders exist only in the editor; the engine never sees them.
/// </summary>
/// <param name="Value">The underlying value.</param>
[JsonConverter(typeof(GuidIdJsonConverter<FolderId>))]
public readonly record struct FolderId(Guid Value) : IGuidId<FolderId>
{
    /// <summary>
    ///     Gets the id that identifies no folder.
    /// </summary>
    public static FolderId Empty { get; }

    /// <summary>
    ///     Gets a value indicating whether this id identifies no folder.
    /// </summary>
    public bool IsEmpty => this.Value == Guid.Empty;

    /// <summary>
    ///     Creates a new, unique folder id.
    /// </summary>
    /// <returns>The new id.</returns>
    public static FolderId New() => new(Guid.NewGuid());

    /// <inheritdoc/>
    public static FolderId FromValue(Guid value) => new(value);

    /// <inheritdoc/>
    public override string ToString() => this.Value.ToString("D");
}
