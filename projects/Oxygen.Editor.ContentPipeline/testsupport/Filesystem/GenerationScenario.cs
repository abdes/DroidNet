// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Testing;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class GenerationScenario
{
    internal static bool ReclaimGeneration(string root)
    {
        using var generation = CookedGeneration.TryClaim(root);
        if (generation is null)
        {
            return !Directory.Exists(root);
        }

        generation.Delete();
        return true;
    }

    internal static ProjectContext PublicationContext(string root) => new()
    {
        ProjectId = Guid.NewGuid(),
        ProjectRoot = root,
        Name = "Publication",
        Category = Category.Games,
        AuthoringMounts = [new("Content", "Content")],
        LocalFolderMounts = [],
        Scenes = [],
    };
    internal static async Task<string> WritePublicationFixtureAsync(ProjectContext project, IAtomicFileStore files, FileVersion expectedHead, CancellationToken token)
    {
        var sourceKey = Guid.CreateVersion7();
        var root = CookPublicationPaths.Generation(project.ProjectRoot, sourceKey);
        _ = Directory.CreateDirectory(root);
        await File.WriteAllBytesAsync(Path.Combine(root, CookedGeneration.MarkerFileName), [], token).ConfigureAwait(false);
        NativeInventoryFixture.WriteIndex(root, [], sourceKey);
        var digest = NativeInventoryFixture.Read(root).IndexSha256;
        var document = new CookPublicationDocument(CookPublicationDocument.CurrentVersion, project.ProjectId, Guid.CreateVersion7(), DateTimeOffset.UtcNow,
            CookPublicationDocument.ConfigurationIdentity(project), [new(CookPublicationRootOwner.Project, "Content", sourceKey, digest, LibraryPath: null)], [], CookInputs: null);
        var documentPath = CookPublicationPaths.Document(project.ProjectRoot, document.OperationId);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(documentPath)!);
        var version = await files.WriteAsync(documentPath, JsonSerializer.SerializeToUtf8Bytes(document, CookPublicationDocument.JsonOptions), FileVersion.Missing, token).ConfigureAwait(false);
        var head = new CookPublicationHead(CookPublicationHead.CurrentVersion, document.OperationId, version.Sha256);
        _ = await files.WriteAsync(CookPublicationPaths.Head(project.ProjectRoot), JsonSerializer.SerializeToUtf8Bytes(head, CookPublicationDocument.JsonOptions), expectedHead, token).ConfigureAwait(false);
        return root;
    }
}
