// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Testing;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed partial class PublicationProject : IDisposable
{
    private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("oxygen-publication-");
    private readonly bool hadPrevious;
    private readonly ImmutableArray<CookPublicationRoot> originalRoots;
    private readonly byte[]? originalHead;
    private FileStream? ownership;
    private CookPublicationReadLease? baseline;
    private Func<string, Task> checkpoint = static _ => Task.CompletedTask;

    public PublicationProject(bool hadPrevious)
    {
        this.hadPrevious = hadPrevious;
        var info = new ProjectInfo(Guid.NewGuid(), "Publication", Category.Games, this.Root)
        {
            AuthoringMounts = [new("Content", "Content"), new("Second", "Second"), new("Unrelated", "Unrelated")],
        };
        this.Context = ProjectContext.FromProjectInfo(info, []);
        this.Operation = new(Guid.NewGuid(), this.Context, 1);
        this.Write("Project.oxy", ProjectInfo.ToJson(info));
        var roots = ImmutableArray.CreateBuilder<CookPublicationRoot>();
        if (hadPrevious)
        {
            foreach (var mount in new[] { "Content", "Second", "Unrelated" })
            {
                var key = Guid.CreateVersion7();
                var root = CookPublicationPaths.Generation(this.Root, key);
                Directory.CreateDirectory(root);
                File.WriteAllText(Path.Combine(root, "value.txt"), "old:" + mount);
                File.WriteAllText(Path.Combine(root, "keep.bin"), "keep:" + mount);
                NativeInventoryFixture.WriteIndex(root, [], key);
                File.WriteAllBytes(Path.Combine(root, CookedGeneration.MarkerFileName), []);
                roots.Add(new(CookPublicationRootOwner.Project, mount, key, NativeInventoryFixture.Read(root).IndexSha256, LibraryPath: null));
            }

            var document = new CookPublicationDocument(
                CookPublicationDocument.CurrentVersion,
                this.Context.ProjectId,
                Guid.NewGuid(),
                DateTimeOffset.UtcNow,
                CookPublicationDocument.ConfigurationIdentity(this.Context),
                roots.ToImmutable(),
                [],
                CookInputs: null);
            var bytes = JsonSerializer.SerializeToUtf8Bytes(document, CookPublicationDocument.JsonOptions);
            var path = CookPublicationPaths.Document(this.Root, document.OperationId);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllBytes(path, bytes);
            this.originalHead = JsonSerializer.SerializeToUtf8Bytes(
                new CookPublicationHead(
                    CookPublicationHead.CurrentVersion,
                    document.OperationId,
                    Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes))),
                CookPublicationDocument.JsonOptions);
            File.WriteAllBytes(CookPublicationPaths.Head(this.Root), this.originalHead);
        }

        this.originalRoots = roots.ToImmutable();
    }

    public string Root => this.directory.FullName;

    public ProjectContext Context { get; }

    public ContentCookOperation Operation { get; }

    public NativeAtomicFileStore Files { get; } = new(new RealFileSystem());

    public ProjectManagerService Manager { get; } = new(new NativeStorageProvider(new RealFileSystem()));

    public CookPublicationReadLease Baseline => this.baseline ?? throw new InvalidOperationException("Prepare staging first.");

    public async Task<CookStagingArea> StageAsync(CancellationToken cancellationToken)
    {
        this.ownership = CookOutputLease.AcquireOperation(this.Root, this.Operation.OperationId);
        using (var gate = await CookOutputLease.AcquireWriteAsync(this.Root, cancellationToken).ConfigureAwait(false))
        {
            this.baseline = await CookPublicationReadLease.OpenUnderGateAsync(this.Context, this.Files, gate, cancellationToken).ConfigureAwait(false);
        }

        var staging = await CookStagingArea.CreateAsync(
            this.Operation,
            this.baseline,
            ["Content", "Second"],
            this.Files,
            this.Manager,
            cancellationToken,
            checkpoint: name => this.checkpoint(name)).ConfigureAwait(false);
        foreach (var root in staging.Roots)
        {
            await File.WriteAllTextAsync(Path.Combine(root.Path, "value.txt"), "new:" + root.Mount, cancellationToken).ConfigureAwait(false);
            NativeInventoryFixture.WriteIndex(root.Path, [], root.SourceKey);
            var opening = await CookOutputReadLease.AcquireAsync(root.Path, cancellationToken).ConfigureAwait(false);
            root.AcceptVerification(opening, NativeInventoryFixture.Read(root.Path));
        }

        return staging;
    }

    public async Task<CookPublicationTransaction> PrepareAsync(
        CookStagingArea staging,
        CancellationToken cancellationToken,
        Func<string,
        Task>? checkpoint = null,
        CookSourceReplacement? sourceReplacement = null,
        ImmutableArray<CookProducedSourceFile> producedSourceFiles = default)
    {
        this.checkpoint = checkpoint ?? (static _ => Task.CompletedTask);
        var replacements = staging.SealRoots();
        var roots = staging.Baseline.Roots.Where(root => !replacements.Any(replacement => string.Equals(replacement.Name, root.Name, StringComparison.Ordinal))).Concat(replacements).ToImmutableArray();
        var document = new CookPublicationDocument(
            CookPublicationDocument.CurrentVersion,
            this.Context.ProjectId,
            this.Operation.OperationId,
            DateTimeOffset.UtcNow,
            CookPublicationDocument.ConfigurationIdentity(this.Context),
            roots,
            [],
            CookInputs: null);
        await staging.Transaction.PrepareAsync(
            this.Operation,
            document,
            sourceReplacement,
            producedSourceFiles.IsDefault ? [] : producedSourceFiles,
            projectChange: null,
            cancellationToken).ConfigureAwait(false);
        staging.RetainForPublication();
        return staging.Transaction;
    }

    public void AssertOld()
    {
        var head = CookPublicationPaths.Head(this.Root);
        if (this.hadPrevious)
        {
            _ = File.ReadAllBytes(head).Should().Equal(this.originalHead!);
            foreach (var root in this.originalRoots)
            {
                _ = File.ReadAllText(Path.Combine(root.ResolvePath(this.Root), "value.txt")).Should().Be("old:" + root.Name);
            }
        }
        else
        {
            _ = File.Exists(head).Should().BeFalse();
        }
    }

    public void AssertNew()
    {
        var head = JsonSerializer.Deserialize<CookPublicationHead>(File.ReadAllBytes(CookPublicationPaths.Head(this.Root)), CookPublicationDocument.JsonOptions)!;
        _ = head.PublicationId.Should().Be(this.Operation.OperationId);
        var document = JsonSerializer.Deserialize<CookPublicationDocument>(File.ReadAllBytes(CookPublicationPaths.Document(this.Root, head.PublicationId)), CookPublicationDocument.JsonOptions)!;
        foreach (var mount in new[] { "Content", "Second" })
        {
            var root = document.Roots.Single(root => string.Equals(root.Name, mount, StringComparison.Ordinal));
            _ = File.ReadAllText(Path.Combine(root.ResolvePath(this.Root), "value.txt")).Should().Be("new:" + mount);
            if (this.hadPrevious)
            {
                _ = File.ReadAllText(Path.Combine(root.ResolvePath(this.Root), "keep.bin")).Should().Be("keep:" + mount);
            }
        }

        if (this.hadPrevious)
        {
            _ = document.Roots.Single(root => string.Equals(root.Name, "Unrelated", StringComparison.Ordinal)).Should().Be(this.originalRoots.Single(root => string.Equals(root.Name, "Unrelated", StringComparison.Ordinal)));
        }
    }

    public void Dispose()
    {
        this.baseline?.Dispose();
        this.ownership?.Dispose();
        this.directory.Delete(recursive: true);
    }

    private void Write(string relative, string text)
    {
        var path = Path.Combine(this.Root, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, text);
    }
}
