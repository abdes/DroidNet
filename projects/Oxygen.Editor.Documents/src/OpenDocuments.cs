// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using DroidNet.Storage;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.Documents;

/// <summary>The documents open in an editing session, each file at most once, each with the kind for its suffix.</summary>
/// <remarks>Used from the thread that edits the documents.</remarks>
public sealed partial class OpenDocuments : IDisposable
{
    private readonly IAtomicFileStore files;
    private readonly ILoggerFactory loggerFactory;
    private readonly ILogger logger;
    private readonly List<Document> documents = [];
    private readonly Dictionary<string, Task<DocumentOpenResult>> opening = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Initializes a new instance of the <see cref="OpenDocuments"/> class.</summary>
    /// <param name="kinds">The registered document kinds; no two open the same suffix.</param>
    /// <param name="files">The store that reads and writes the documents' files.</param>
    /// <param name="loggerFactory">The logger factory, or null to log nothing.</param>
    /// <exception cref="ArgumentException">A suffix does not start with a dot, or two kinds open it.</exception>
    public OpenDocuments(IEnumerable<IDocumentKind> kinds, IAtomicFileStore files, ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(kinds);
        ArgumentNullException.ThrowIfNull(files);
        this.Kinds = [.. kinds];
        this.files = files;
        this.loggerFactory = loggerFactory ?? NullLoggerFactory.Instance;
        this.logger = this.loggerFactory.CreateLogger<OpenDocuments>();
        ValidateSuffixes(this.Kinds);
    }

    /// <summary>Occurs when a document opens.</summary>
    public event EventHandler<DocumentEventArgs>? Opened;

    /// <summary>Occurs when a document closed; it is disposed once the event returns.</summary>
    public event EventHandler<DocumentEventArgs>? Closed;

    /// <summary>Gets the registered document kinds.</summary>
    public IReadOnlyList<IDocumentKind> Kinds { get; }

    /// <summary>Gets the open documents.</summary>
    public IReadOnlyList<Document> Documents => this.documents;

    /// <summary>Finds the kind that opens a file: the one with the longest suffix that ends its name.</summary>
    /// <param name="path">The file's path.</param>
    /// <returns>The kind, or <see langword="null"/> when no kind opens the file.</returns>
    public IDocumentKind? FindKind(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        return this.Kinds
            .Select(kind => (Kind: kind, Suffix: DocumentKinds.SuffixOf(kind, path)))
            .Where(static match => match.Suffix is not null)
            .OrderByDescending(static match => match.Suffix!.Length)
            .Select(static match => match.Kind)
            .FirstOrDefault();
    }

    /// <summary>Finds the open document that edits a file.</summary>
    /// <param name="path">The file's path.</param>
    /// <returns>The document, or <see langword="null"/> when the file is not open.</returns>
    public Document? Find(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        return this.documents.FirstOrDefault(document => document.File.IsAt(path));
    }

    /// <summary>Finds an open document by its id.</summary>
    /// <param name="id">The document's id.</param>
    /// <returns>The document, or <see langword="null"/> when no open document has that id.</returns>
    public Document? Find(DocumentId id) => this.documents.FirstOrDefault(document => document.Id == id);

    /// <summary>Finds the open document that edits an asset.</summary>
    /// <param name="asset">The asset's URI, in the catalog's form, as every <see cref="DocumentFile"/> holds it.</param>
    /// <returns>The document, or <see langword="null"/> when the asset is not open.</returns>
    public Document? FindByAsset(Uri asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        return this.documents.FirstOrDefault(document => document.File.AssetUri == asset);
    }

    /// <summary>Opens a file, or finds the document that already edits it.</summary>
    /// <param name="file">The file to open.</param>
    /// <param name="cancellationToken">Cancels waiting for the open; another request for the file still gets it.</param>
    /// <returns>The document, or why the file did not open.</returns>
    public async Task<DocumentOpenResult> OpenAsync(DocumentFile file, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(file);
        if (this.Find(file.Path) is { } open)
        {
            return DocumentOpenResult.AlreadyOpen(open);
        }

        if (!this.opening.TryGetValue(file.Path, out var pending))
        {
            // An open that ends before its first wait has already left the pending opens.
            pending = this.OpenNewAsync(file);
            if (!pending.IsCompleted)
            {
                this.opening.Add(file.Path, pending);
            }
        }

        return await pending.WaitAsync(cancellationToken).ConfigureAwait(true);
    }

    /// <summary>Closes a document and disposes it, with its content and its history.</summary>
    /// <param name="document">The document to close.</param>
    /// <remarks>Asking about unsaved edits is the caller's: closing discards them.</remarks>
    public void Close(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        if (!this.documents.Remove(document))
        {
            return;
        }

        this.LogClosed(document.Kind.Name, document.Title, document.IsDirty);
        this.Closed?.Invoke(this, new DocumentEventArgs(document));
        document.Dispose();
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        foreach (var document in this.documents.ToArray())
        {
            this.Close(document);
        }
    }

    private static void ValidateSuffixes(IReadOnlyList<IDocumentKind> kinds)
    {
        var owners = new Dictionary<string, IDocumentKind>(StringComparer.OrdinalIgnoreCase);
        foreach (var kind in kinds)
        {
            foreach (var suffix in kind.FileSuffixes)
            {
                if (suffix.Length < 2 || suffix[0] != '.')
                {
                    throw new ArgumentException($"The {kind.Name} suffix '{suffix}' does not start with a dot.", nameof(kinds));
                }

                if (!owners.TryAdd(suffix, kind))
                {
                    throw new ArgumentException($"The {kind.Name} and {owners[suffix].Name} kinds both open '{suffix}'.", nameof(kinds));
                }
            }
        }
    }

    private async Task<DocumentOpenResult> OpenNewAsync(DocumentFile file)
    {
        try
        {
            if (this.FindKind(file.Path) is not { } kind)
            {
                return this.OpenFailed(file, $"No editor opens '{Path.GetFileName(file.Path)}'.");
            }

            var snapshot = await this.files.ReadAsync(file.Path).ConfigureAwait(true);
            if (!snapshot.Version.Exists)
            {
                return this.OpenFailed(file, $"'{file.Path}' does not exist.");
            }

            var decoded = await kind.DecodeAsync(file, snapshot.Content.AsMemory(), CancellationToken.None).ConfigureAwait(true);
            if (decoded.Content is not { } content)
            {
                return this.OpenFailed(file, decoded.Problem!);
            }

            var document = new Document(kind, file, content, snapshot.Version, this.files, this.loggerFactory.CreateLogger<Document>());
            this.documents.Add(document);
            this.LogOpened(kind.Name, document.Title, file.Path);
            this.Opened?.Invoke(this, new DocumentEventArgs(document));
            return DocumentOpenResult.Opened(document);
        }
        finally
        {
            _ = this.opening.Remove(file.Path);
        }
    }

    private DocumentOpenResult OpenFailed(DocumentFile file, string problem)
    {
        this.LogOpenFailed(file.Path, problem);
        return DocumentOpenResult.Failed(problem);
    }
}
