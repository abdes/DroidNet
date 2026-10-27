// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class PublicationReaderTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Publication can finish while an earlier inspection keeps its exact generation alive.</summary>
    /// <returns>The nested-reader deadlock regression.</returns>
    [TestMethod]
    public async Task PublicationCompletesWhilePriorInspectionRemainsUsable()
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        cancellation.CancelAfter(TimeSpan.FromSeconds(5));
        var staging = await project.StageAsync(cancellation.Token).ConfigureAwait(false);
        await using var stagingLifetime = staging.ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, cancellation.Token).ConfigureAwait(false);
        using var reader = project.Baseline.Retain();
        using var accepted = await transaction.PublishAsync(preview: null, project.Baseline, static () => { }, cancellation.Token).WaitAsync(cancellation.Token).ConfigureAwait(false);
        var oldRoot = reader.FindProjectRoot("Content")!.FullPath;
        _ = (reader.PublicationId == accepted.PublicationId).Should().BeFalse();
        var catalog = await reader.CreateCatalogAsync(reader.Roots.Single(static root => string.Equals(root.Name, "Content", StringComparison.Ordinal)), cancellation.Token).ConfigureAwait(false);
        _ = (await File.ReadAllTextAsync(Path.Combine(oldRoot, "value.txt"), cancellation.Token).ConfigureAwait(false)).Should().Be("old:Content");
        using var claimed = CookedGeneration.TryClaim(oldRoot);
        _ = claimed.Should().BeNull();
        project.AssertNew();
    }
}
