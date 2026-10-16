// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Documents;

/// <summary>
///     Identifies an open document for its lifetime, and its tab: a document has at most one tab, keyed by this id. It is
///     not stored: reopening the same file opens a new document with a new id.
/// </summary>
/// <param name="Value">The underlying value.</param>
public readonly record struct DocumentId(Guid Value)
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

    /// <summary>
    ///     Creates a document id from its underlying value.
    /// </summary>
    /// <param name="value">The underlying value.</param>
    /// <returns>The document id.</returns>
    public static DocumentId FromValue(Guid value) => new(value);

    /// <inheritdoc/>
    public override string ToString() => this.Value.ToString("D");
}
