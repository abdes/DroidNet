// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text;
using DroidNet.Storage;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

/// <summary>A minimal document kind for the Shell's tests: a text note held in memory.</summary>
internal sealed class Notes : IDisposable
{
    public Notes() => this.Documents = new OpenDocuments([this.Kind], this.Files);

    public NoteKind Kind { get; } = new();

    public MemoryStore Files { get; } = new();

    public OpenDocuments Documents { get; }

    public async Task<Document> OpenAsync(string name, CancellationToken cancellationToken)
    {
        var path = Path.Combine(Path.GetTempPath(), "notes", name + ".note");
        _ = await this.Files.WriteAsync(path, Encoding.UTF8.GetBytes(name), FileVersion.Missing, cancellationToken).ConfigureAwait(false);
        var opened = await this.Documents.OpenAsync(new DocumentFile(path, new Uri($"asset:///Content/{name}.note")), cancellationToken).ConfigureAwait(false);
        return opened.Document ?? throw new InvalidOperationException(opened.Problem);
    }

    public void Dispose() => this.Documents.Dispose();

    internal sealed class NoteKind : IDocumentKind
    {
        public string Name => "Note";

        public IReadOnlyList<string> FileSuffixes { get; } = [".note"];

        public Task<DocumentDecodeResult> DecodeAsync(DocumentFile file, ReadOnlyMemory<byte> content, DocumentDecodeContext context, CancellationToken cancellationToken)
            => Task.FromResult(DocumentDecodeResult.Decoded(new NoteContent()));
    }

    internal sealed class NoteContent : IDocumentContent
    {
        public bool HasEditsInProgress => false;

        public Task FinishEditsAsync(CancellationToken cancellationToken) => Task.CompletedTask;

        public DocumentCapture Capture() => DocumentCapture.Of(_ => Task.FromResult<ReadOnlyMemory<byte>>(Encoding.UTF8.GetBytes("saved")));

        public DocumentCapture CaptureCopy(DocumentFile copy) => this.Capture();

        public void ApplyRelocation(DocumentRelocation relocation)
        {
        }
    }

    internal sealed class MemoryStore : IAtomicFileStore
    {
        private readonly ConcurrentDictionary<string, byte[]> files = new(StringComparer.OrdinalIgnoreCase);

        public Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default)
            => Task.FromResult(this.files.TryGetValue(path, out var content)
                ? new FileSnapshot([.. content], VersionOf(content))
                : new FileSnapshot(ImmutableArray<byte>.Empty, FileVersion.Missing));

        public Task<FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> content, FileVersion expected, CancellationToken cancellationToken = default)
        {
            var bytes = content.ToArray();
            this.files[path] = bytes;
            return Task.FromResult(VersionOf(bytes));
        }

        public void Delete(string path) => _ = this.files.TryRemove(path, out _);

        private static FileVersion VersionOf(byte[] content) => new(Exists: true, Convert.ToHexString(SHA256.HashData(content)));
    }
}
