// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>What a save writes, taken from a document's content, or why it cannot be saved.</summary>
public sealed class DocumentCapture
{
    private readonly Func<CancellationToken, Task<ReadOnlyMemory<byte>>>? encode;

    private DocumentCapture(Func<CancellationToken, Task<ReadOnlyMemory<byte>>>? encode, string? refusal)
    {
        this.encode = encode;
        this.Refusal = refusal;
    }

    /// <summary>Gets why the content cannot be saved, or <see langword="null"/> when it can.</summary>
    public string? Refusal { get; }

    /// <summary>Creates a capture that encodes what was taken.</summary>
    /// <param name="encode">Turns what was taken into the file's bytes; it may run on any thread.</param>
    /// <returns>The capture.</returns>
    public static DocumentCapture Of(Func<CancellationToken, Task<ReadOnlyMemory<byte>>> encode)
    {
        ArgumentNullException.ThrowIfNull(encode);
        return new(encode, refusal: null);
    }

    /// <summary>Creates a capture that refuses the save.</summary>
    /// <param name="reason">Why the content cannot be saved, as people read it.</param>
    /// <returns>The capture.</returns>
    public static DocumentCapture Refused(string reason)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(reason);
        return new(encode: null, reason);
    }

    /// <summary>Encodes what was taken.</summary>
    /// <param name="cancellationToken">Cancels encoding.</param>
    /// <returns>The file's bytes.</returns>
    internal Task<ReadOnlyMemory<byte>> EncodeAsync(CancellationToken cancellationToken)
        => this.encode is { } encoder
            ? encoder(cancellationToken)
            : throw new InvalidOperationException("A refused capture has nothing to encode.");
}
