// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Documents;

/// <summary>
///     Identifies an open document for its lifetime. It is not stored: reopening the same file opens a new document with a
///     new id.
/// </summary>
/// <param name="Value">The underlying value.</param>
[JsonConverter(typeof(GuidIdJsonConverter<DocumentId>))]
public readonly record struct DocumentId(Guid Value) : IGuidId<DocumentId>
{
    /// <summary>
    ///     Gets the id that identifies no document.
    /// </summary>
    public static DocumentId Empty { get; }

    /// <summary>
    ///     Gets a value indicating whether this id identifies no document.
    /// </summary>
    public bool IsEmpty => this.Value == Guid.Empty;

    /// <summary>
    ///     Creates a new, unique document id.
    /// </summary>
    /// <returns>The new id.</returns>
    public static DocumentId New() => new(Guid.NewGuid());

    /// <inheritdoc/>
    public static DocumentId FromValue(Guid value) => new(value);

    /// <inheritdoc/>
    public override string ToString() => this.Value.ToString("D");
}
