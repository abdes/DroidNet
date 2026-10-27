// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>What a save writes, taken from a document's content, or why it cannot be saved.</summary>
public sealed class DocumentCapture
{
    private readonly Func<CancellationToken, Task<ReadOnlyMemory<byte>>>? encode;
    private readonly Func<CancellationToken, Task<DocumentSaveResult>>? store;

    private DocumentCapture(Func<CancellationToken, Task<ReadOnlyMemory<byte>>>? encode, Func<CancellationToken, Task<DocumentSaveResult>>? store, string? refusal)
    {
        this.encode = encode;
        this.store = store;
        this.Refusal = refusal;
    }

    /// <summary>Gets why the content cannot be saved, or <see langword="null"/> when it can.</summary>
    public string? Refusal { get; }

    /// <summary>Gets a value indicating whether the content stores what was taken itself, outside the document's file.</summary>
    internal bool IsStored => this.store is not null;

    /// <summary>Creates a capture that encodes what was taken.</summary>
    /// <param name="encode">Turns what was taken into the file's bytes; it may run on any thread.</param>
    /// <returns>The capture.</returns>
    public static DocumentCapture Of(Func<CancellationToken, Task<ReadOnlyMemory<byte>>> encode)
    {
        ArgumentNullException.ThrowIfNull(encode);
        return new(encode, store: null, refusal: null);
    }

    /// <summary>
    /// Creates a capture whose content keeps what was taken in a file of its own, such as a model's import record, for a
    /// model source the editor never writes; the document's file is left as it is.
    /// </summary>
    /// <param name="store">Writes what was taken and says how it went: <see cref="DocumentSaveOutcome.Conflict"/> when its
    /// file changed since the content read it, <see cref="DocumentSaveOutcome.Refused"/> when it could not be written.</param>
    /// <returns>The capture.</returns>
    public static DocumentCapture Stored(Func<CancellationToken, Task<DocumentSaveResult>> store)
    {
        ArgumentNullException.ThrowIfNull(store);
        return new(encode: null, store, refusal: null);
    }

    /// <summary>Creates a capture that refuses the save.</summary>
    /// <param name="reason">Why the content cannot be saved, as people read it.</param>
    /// <returns>The capture.</returns>
    public static DocumentCapture Refused(string reason)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(reason);
        return new(encode: null, store: null, reason);
    }

    /// <summary>Encodes what was taken.</summary>
    /// <param name="cancellationToken">Cancels encoding.</param>
    /// <returns>The file's bytes.</returns>
    internal Task<ReadOnlyMemory<byte>> EncodeAsync(CancellationToken cancellationToken)
        => this.encode is { } encoder
            ? encoder(cancellationToken)
            : throw new InvalidOperationException("Only a capture of the document's file has bytes to encode.");

    internal Task<DocumentSaveResult> StoreAsync(CancellationToken cancellationToken)
        => this.store is { } writer
            ? writer(cancellationToken)
            : throw new InvalidOperationException("Only a stored capture stores itself.");
}
