// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Documents;
using Oxygen.Managed.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Snapshots;

/// <summary>Checks what the cook learns about open documents: their files, their unsaved edits and their read leases.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class OpenDocumentCookInputsTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "OxygenOpenDocumentCookInputsTests", Guid.NewGuid().ToString("N"));
    private readonly CookDocumentRegistry registry = new();
    private readonly OpenDocuments documents;
    private readonly OpenDocumentCookInputs inputs;

    /// <summary>Initializes a new instance of the <see cref="OpenDocumentCookInputsTests"/> class.</summary>
    public OpenDocumentCookInputsTests()
    {
        _ = Directory.CreateDirectory(this.root);
        this.documents = new OpenDocuments([new NoteKind()], new NativeAtomicFileStore(new RealFileSystem()));
        this.inputs = new OpenDocumentCookInputs(this.documents, this.registry);
    }

    /// <summary>Gets or sets the test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <inheritdoc/>
    public void Dispose()
    {
        this.inputs.Dispose();
        this.documents.Dispose();
        Directory.Delete(this.root, recursive: true);
    }

    /// <summary>An open document is registered by its file as saved, and leaves the registry when it closes.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ADocumentIsRegisteredWhileItIsOpen()
    {
        var document = await this.OpenAsync("Intro").ConfigureAwait(false);

        var state = this.registry.GetState().Documents.Should().ContainSingle().Which;
        _ = state.SourcePath.Should().Be(document.File.Path);
        _ = state.DisplayName.Should().Be("Intro");
        _ = state.IsDirty.Should().BeFalse();
        _ = state.SavedContentHash.Should().Be(document.Version.Sha256);

        this.documents.Close(document);

        _ = this.registry.GetState().Documents.Should().BeEmpty();
    }

    /// <summary>An edit in progress makes the document dirty for the cook, before it is recorded in the history.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AnEditInProgressIsReportedDirty()
    {
        var document = await this.OpenAsync("Intro").ConfigureAwait(false);
        ((NoteContent)document.Content).EditInProgress = true;

        using var reads = await this.registry.AcquireAsync([document.File.Path], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = reads.Documents.Should().ContainSingle().Which.IsDirty.Should().BeTrue();
    }

    /// <summary>A document that follows a relocation is registered by its new file.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ARelocatedDocumentIsRegisteredByItsNewFile()
    {
        var document = await this.OpenAsync("Intro").ConfigureAwait(false);
        var moved = Path.Combine(this.root, "Opening.note");
        File.Move(document.File.Path.FullPath, moved);

        await document.FollowRelocationAsync(
            new DocumentRelocation(new DocumentFile(FileSystemPath.From(moved), AssetPath.Parse("/Content/Opening.note")), document.Version, static _ => null),
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = this.registry.GetState().Documents.Should().ContainSingle().Which.SourcePath.Should().Be(FileSystemPath.From(moved));
    }

    /// <summary>A read lease is taken on the thread that opened the document, and holds off its reload until released.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task TheReadLeaseIsTakenOnTheDocumentsThread()
    {
        var owner = new CountingContext();
        var document = await this.OpenOnAsync(owner, "Intro").ConfigureAwait(false);

        var posts = owner.Posts;
        using (var reads = await Task.Run(() => this.registry.AcquireAsync([document.File.Path], CancellationToken.None)).ConfigureAwait(false))
        {
            _ = reads.Documents.Should().ContainSingle();
            _ = owner.Posts.Should().BeGreaterThan(posts);
        }
    }

    /// <summary>Cancelling a cook ends its wait for a lease, and the lease acquired after it is released.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CancellingEndsTheWaitForALease()
    {
        var owner = new CountingContext();
        var document = await this.OpenOnAsync(owner, "Intro").ConfigureAwait(false);
        owner.Hold = true;
        using var cancellation = new CancellationTokenSource();

        var acquiring = this.registry.AcquireAsync([document.File.Path], cancellation.Token);
        await cancellation.CancelAsync().ConfigureAwait(false);

        Func<Task> wait = () => acquiring;
        _ = await wait.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        owner.RunHeld();
        this.documents.Close(document);
        _ = ((NoteContent)document.Content).IsDisposed.Should().BeTrue("the late lease was released, so closing disposes the content");
    }

    private async Task<Document> OpenOnAsync(SynchronizationContext owner, string name)
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(owner);
        try
        {
            return await this.OpenAsync(name).ConfigureAwait(true);
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    private async Task<Document> OpenAsync(string name)
    {
        var path = Path.Combine(this.root, name + ".note");
        await File.WriteAllTextAsync(path, "text", this.TestContext.CancellationToken).ConfigureAwait(true);
        var opened = await this.documents.OpenAsync(new DocumentFile(FileSystemPath.From(path), AssetPath.Parse($"/Content/{name}.note")), this.TestContext.CancellationToken).ConfigureAwait(true);
        return opened.Document ?? throw new InvalidOperationException(opened.Problem);
    }

    // Runs posted work as the current context, at once or, while held, when the test says; and counts it.
    private sealed class CountingContext : SynchronizationContext
    {
        private readonly List<(SendOrPostCallback Callback, object? State)> held = [];
        private int posts;

        public int Posts => Volatile.Read(ref this.posts);

        public bool Hold { get; set; }

        public override void Post(SendOrPostCallback d, object? state)
        {
            _ = Interlocked.Increment(ref this.posts);
            if (this.Hold)
            {
                lock (this.held)
                {
                    this.held.Add((d, state));
                }

                return;
            }

            this.Run(d, state);
        }

        public void RunHeld()
        {
            this.Hold = false;
            (SendOrPostCallback Callback, object? State)[] work;
            lock (this.held)
            {
                work = [.. this.held];
                this.held.Clear();
            }

            foreach (var (callback, state) in work)
            {
                this.Run(callback, state);
            }
        }

        private void Run(SendOrPostCallback d, object? state)
        {
            var previous = Current;
            SetSynchronizationContext(this);
            try
            {
                d(state);
            }
            finally
            {
                SetSynchronizationContext(previous);
            }
        }
    }

    private sealed class NoteKind : IDocumentKind
    {
        public string Name => "Note";

        public IReadOnlyList<string> FileSuffixes { get; } = [".note"];

        public Task<DocumentDecodeResult> DecodeAsync(DocumentFile file, ReadOnlyMemory<byte> content, DocumentDecodeContext context, CancellationToken cancellationToken)
            => Task.FromResult(DocumentDecodeResult.Decoded(new NoteContent(Encoding.UTF8.GetString(content.Span))));
    }

    private sealed class NoteContent(string text) : IDocumentContent, IDisposable
    {
        public bool EditInProgress { get; set; }

        public bool IsDisposed { get; private set; }

        public bool HasEditsInProgress => this.EditInProgress;

        public Task FinishEditsAsync(CancellationToken cancellationToken) => Task.CompletedTask;

        public DocumentCapture Capture() => DocumentCapture.Of(_ => Task.FromResult<ReadOnlyMemory<byte>>(Encoding.UTF8.GetBytes(text)));

        public DocumentCapture CaptureCopy(DocumentFile copy) => this.Capture();

        public void ApplyRelocation(DocumentRelocation relocation)
        {
        }

        public void Dispose() => this.IsDisposed = true;
    }
}
