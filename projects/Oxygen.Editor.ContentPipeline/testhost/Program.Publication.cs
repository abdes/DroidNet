// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline.WorkerProbe;

/// <summary>Runs the production publication transaction in a terminable test process.</summary>
internal static partial class Program
{
    private static async Task<int> PublishUntilBoundaryAsync(string[] args)
    {
        var project = new ProjectContext
        {
            ProjectId = Guid.Parse(args[2]), ProjectRoot = args[1], Name = "Publication", Category = Category.Games,
            AuthoringMounts = [new("Content", "Content"), new("Second", "Second")], LocalFolderMounts = [], Scenes = [],
        };
        var operation = new ContentCookOperation(Guid.Parse(args[3]), project, 1);
        var boundary = args[4];
        using var staging = await CookStagingArea.CreateAsync(operation, ["Content", "Second"], CancellationToken.None).ConfigureAwait(false);
        foreach (var root in staging.Roots)
        {
            await File.WriteAllTextAsync(Path.Combine(root.StagingPath, "container.index.bin"), "new:" + root.Mount, CancellationToken.None).ConfigureAwait(false);
        }

        var files = new NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem());
        var transaction = await CookPublicationTransaction.PrepareAsync(
            operation,
            staging,
            new Dictionary<string, byte[]>(StringComparer.Ordinal)
            {
                [CookPublicationTransaction.PublicationMetadata] = Encoding.UTF8.GetBytes("new-receipt"),
                [CookPublicationTransaction.ProvenanceMetadata] = Encoding.UTF8.GetBytes("new-provenance"),
            },
            files,
            CancellationToken.None,
            name => string.Equals(name, boundary, StringComparison.Ordinal) ? SignalAndWaitAsync(name) : Task.CompletedTask).ConfigureAwait(false);
        if (string.Equals(boundary, "Prepared", StringComparison.Ordinal))
        {
            await SignalAndWaitAsync(boundary).ConfigureAwait(false);
        }

        await transaction.PublishAsync(preview: null, static () => { }, CancellationToken.None).ConfigureAwait(false);
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
