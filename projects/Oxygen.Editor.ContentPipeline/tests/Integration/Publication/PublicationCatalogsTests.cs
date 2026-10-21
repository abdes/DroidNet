// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

/// <summary>Checks the catalogs a reader gets for a project's cooked roots.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class PublicationCatalogsTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A library re-cooked after the project was published makes the reader publish the project again and return that publication.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RecookedLibraryIsPublishedAgainBeforeItsCatalogsAreReturned()
    {
        using var consumer = new CookWorkspace();
        var library = Directory.CreateTempSubdirectory("Oxygen-PublicationCatalogs-");
        try
        {
            WriteLibrary(library.FullName, 1);
            consumer.Activate(consumer.ProjectContext with { LocalFolderMounts = [new("Library", FileSystemPath.From(library.FullName))] });
            var project = consumer.Session.Context;
            using var published = await consumer.Publication.AcquireForMountAsync(project, this.TestContext.CancellationToken).ConfigureAwait(false);
            var recorded = published.Roots.Single(static root => root.Name == "Library");

            WriteLibrary(library.FullName, 2);
            Func<Task> exact = () => published.CreateCatalogsAsync(this.TestContext.CancellationToken);
            _ = await exact.Should().ThrowAsync<CookedLibraryChangedException>("a publication read exactly is never published again").ConfigureAwait(false);

            var opened = await consumer.Publication.AcquireCatalogsAsync(project, this.TestContext.CancellationToken).ConfigureAwait(false);
            using var publication = opened.Publication;
            try
            {
                _ = (publication.PublicationId == published.PublicationId).Should().BeFalse("the project was published again");
                _ = publication.Roots.Single(static root => root.Name == "Library").IndexSha256.Should().NotBe(recorded.IndexSha256);
                _ = opened.Catalogs.Should().HaveCount(publication.Roots.Length - publication.UnavailableRoots.Length);
            }
            finally
            {
                foreach (var catalog in opened.Catalogs)
                {
                    (catalog as IDisposable)?.Dispose();
                }
            }
        }
        finally
        {
            library.Delete(recursive: true);
        }
    }

    // Writes a cooked library the way an external cook does: a new source identity and payload each time.
    private static void WriteLibrary(string root, byte value)
    {
        _ = Directory.CreateDirectory(Path.Combine(root, "Materials"));
        byte[] bytes = [value, value, value];
        File.WriteAllBytes(Path.Combine(root, "Materials", "Shared.omat"), bytes);
        using var index = File.Create(Path.Combine(root, "container.index.bin"));
        Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(index, new Document(1, IndexFeatures.HasVirtualPaths, Guid.CreateVersion7(), [new(new AssetKey(1, 2), "Materials/Shared.omat", "/Library/Materials/Shared.omat", 1, (ulong)bytes.Length, SHA256.HashData(bytes))], []));
    }
}
