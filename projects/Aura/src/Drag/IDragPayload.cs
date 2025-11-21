// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Drag;

/// <summary>
///     Represents a payload object that can participate in drag-and-drop operations across tab
///     strips with support for tear-out and cloning.
/// </summary>
/// <remarks>
///     <para><b>Shallow Clone Semantics:</b></para>
///     <para>
///     During tear-out, the coordinator needs two distinct object references: one for the source
///     strip (being removed async) and one for drag operations (being inserted). <see
///     cref="ShallowClone"/> creates a new top-level object with distinct reference identity but
///     preserves nested object references (ViewModel, Document, etc.). This prevents the "object in
///     two places" race condition while keeping the underlying content singular. The <see
///     cref="ContentId"/> must remain stable across clones for reconciliation.
///     </para>
///     <para><b>Equality Semantics:</b></para>
///     <para>
///     Equality and hash code are based on <see cref="ContentId"/>, not reference identity. This
///     allows collection operations like <c>Items.IndexOf(clone)</c> to find the original payload
///     even though they are distinct object references. Code that needs reference-specific behavior
///     can explicitly use <see cref="object.ReferenceEquals(object, object)"/>.
///     </para>
/// </remarks>
public interface IDragPayload : IEquatable<IDragPayload>
{
    /// <summary>
    ///     Gets the stable identifier for the payload's content. Implementations must preserve
    ///     this value across shallow clones so the original and clone can be reconciled.
    /// </summary>
    public Guid ContentId { get; }

    /// <summary>
    ///     Gets a human-readable title for UI and logging.
    /// </summary>
    public string Title { get; }

    /// <summary>
    ///     Gets a value indicating whether the item can be closed.
    ///     Non-closable items cannot be torn out into new windows.
    /// </summary>
    public bool IsClosable { get; }

    /// <summary>
    ///     Produces a shallow clone with a distinct object identity while preserving the underlying
    ///     content references and <see cref="ContentId"/>. The clone enables safe tear-out and
    ///     re-insertion without creating ambiguous object identity.
    /// </summary>
    /// <returns>A new <see cref="IDragPayload"/> instance with the same <see cref="ContentId"/>.</returns>
    public IDragPayload ShallowClone();
}
