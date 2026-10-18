// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>A document kind for tests: a note names itself on its first line, holds text, then lists references.</summary>
/// <param name="suffixes">The suffixes the kind opens.</param>
[ExcludeFromCodeCoverage]
internal sealed class NoteKind(params string[] suffixes) : IDocumentKind
{
    public const string Suffix = ".note.txt";

    /// <summary>Text that makes a file fail to decode.</summary>
    public const string Unreadable = "!unreadable";

    public NoteKind()
        : this(Suffix)
    {
    }

    public string Name { get; init; } = "Note";

    public IReadOnlyList<string> FileSuffixes { get; } = suffixes;

    public int Decodes { get; private set; }

    public static byte[] Encode(AssetPath identity, string text, params AssetPath[] references)
        => Encoding.UTF8.GetBytes(string.Join('\n', [identity.ToUriString(), text, .. references.Select(static uri => uri.ToUriString())]));

    public async Task<DocumentDecodeResult> DecodeAsync(DocumentFile file, ReadOnlyMemory<byte> content, DocumentDecodeContext context, CancellationToken cancellationToken)
    {
        await Task.Yield();
        this.Decodes++;
        var lines = Encoding.UTF8.GetString(content.Span).Split('\n');
        return lines.Length < 2 || lines[1] == Unreadable
            ? DocumentDecodeResult.Failed($"'{file.Path}' is not a note.")
            : DocumentDecodeResult.Decoded(new NoteContent(AssetPath.ParseUri(lines[0]), lines[1], [.. lines.Skip(2).Select(static line => AssetPath.ParseUri(line))]));
    }
}
