// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class PublicationScenario
{
    internal static async Task CopyPersistedStateAsync(string source, string target, CancellationToken cancellationToken)
    {
        foreach (var path in Directory.EnumerateDirectories(source, "*", SearchOption.AllDirectories))
        {
            _ = Directory.CreateDirectory(Path.Combine(target, Path.GetRelativePath(source, path)));
        }

        foreach (var path in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
        {
            if (Path.GetFileName(path) != CookedGeneration.MarkerFileName && Path.GetExtension(path) is ".lock" or ".lease")
            {
                continue;
            }

            var bytes = await File.ReadAllBytesAsync(path, cancellationToken).ConfigureAwait(false);
            await File.WriteAllBytesAsync(Path.Combine(target, Path.GetRelativePath(source, path)), bytes, cancellationToken).ConfigureAwait(false);
        }
    }

    internal static void AssertRecoveredRoot(string projectRoot, string mount, bool hadPrevious)
    {
        var headPath = CookPublicationPaths.Head(projectRoot);
        if (!hadPrevious)
        {
            _ = File.Exists(headPath).Should().BeFalse();
            return;
        }

        var head = JsonSerializer.Deserialize<CookPublicationHead>(File.ReadAllBytes(headPath), CookPublicationDocument.JsonOptions)!;
        var document = JsonSerializer.Deserialize<CookPublicationDocument>(File.ReadAllBytes(CookPublicationPaths.Document(projectRoot, head.PublicationId)), CookPublicationDocument.JsonOptions)!;
        var root = document.Roots.Single(root => root.Name == mount).ResolvePath(projectRoot);
        _ = File.ReadAllText(Path.Combine(root, "value.txt")).Should().Be("old:" + mount);
        _ = File.ReadAllText(Path.Combine(root, "keep.bin")).Should().Be("keep:" + mount);
    }

    internal const string SourceBundle = "Content/SourceMedia/DCC/Model";
}
