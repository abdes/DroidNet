// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Testing;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.WorkerProbe;

/// <summary>Runs the production publication transaction in a terminable test process.</summary>
internal static partial class Program
{
    private static async Task<int> PublishUntilBoundaryAsync(string[] args)
    {
        var files = new NativeAtomicFileStore(new RealFileSystem());
        var manager = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()), atomicFiles: files);
        var info = await manager.LoadProjectInfoAsync(args[1]).ConfigureAwait(false)
            ?? throw new InvalidDataException("The publication fixture has no saved project.");
        var project = ProjectContext.FromProjectInfo(info, []);
        var operation = new ContentCookOperation(Guid.Parse(args[3]), project, 1);
        var boundary = args[4];
        using var ownership = CookOutputLease.AcquireOperation(project.ProjectRoot, operation.OperationId);
        CookPublicationReadLease baseline;
        using (var gate = await CookOutputLease.AcquireWriteAsync(project.ProjectRoot, CancellationToken.None).ConfigureAwait(false))
        {
            baseline = await CookPublicationReadLease.OpenUnderGateAsync(project, files, gate, CancellationToken.None).ConfigureAwait(false);
        }

        using var baselineLifetime = baseline;
        var staging = await CookStagingArea.CreateAsync(operation, baseline, ["Content", "Second"], files, manager, CancellationToken.None,
            checkpoint: name => name == boundary ? SignalAndWaitAsync(name) : Task.CompletedTask).ConfigureAwait(false);
        await using var stagingLifetime = staging.ConfigureAwait(false);
        foreach (var root in staging.Roots)
        {
            await File.WriteAllTextAsync(Path.Combine(root.Path, "value.txt"), "new:" + root.Mount, CancellationToken.None).ConfigureAwait(false);
            NativeInventoryFixture.WriteIndex(root.Path, [], root.SourceKey);
            var opening = await CookOutputReadLease.AcquireAsync(root.Path, CancellationToken.None).ConfigureAwait(false);
            root.AcceptVerification(opening, NativeInventoryFixture.Read(root.Path));
        }
        var replacements = staging.SealRoots();
        var roots = baseline.Roots.Where(root => !replacements.Any(replacement => replacement.Name == root.Name)).Concat(replacements).ToImmutableArray();
        var document = new CookPublicationDocument(CookPublicationDocument.CurrentVersion, project.ProjectId, operation.OperationId,
            DateTimeOffset.UtcNow, CookPublicationDocument.ConfigurationIdentity(project), roots, [], CookInputs: null);
        await staging.Transaction.PrepareAsync(operation, document, sourceReplacement: null, [], projectChange: null, CancellationToken.None).ConfigureAwait(false);
        staging.RetainForPublication();
        if (string.Equals(boundary, "Prepared", StringComparison.Ordinal))
        {
            await SignalAndWaitAsync(boundary).ConfigureAwait(false);
        }

        using var accepted = await staging.Transaction.PublishAsync(preview: null, baseline, static () => { }, CancellationToken.None).ConfigureAwait(false);
        if (string.Equals(boundary, "Committed", StringComparison.Ordinal))
        {
            await SignalAndWaitAsync(boundary).ConfigureAwait(false);
        }

        return 0;
    }

    private static async Task SignalAndWaitAsync(string boundary)
    {
        await Console.Out.WriteLineAsync("PUBLICATION_BOUNDARY:" + boundary).ConfigureAwait(false);
        await Console.Out.FlushAsync().ConfigureAwait(false);
        _ = await Console.In.ReadLineAsync().ConfigureAwait(false);
    }
}
