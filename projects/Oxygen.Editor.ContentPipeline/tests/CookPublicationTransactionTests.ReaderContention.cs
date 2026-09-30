// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Old immutable readers do not block publication of a replacement.</summary>
public sealed partial class CookPublicationTransactionTests
{
    /// <summary>Publication can finish while an earlier inspection keeps its exact generation alive.</summary>
    /// <returns>The nested-reader deadlock regression.</returns>
    [TestMethod]
    public async Task PublicationCompletesWhilePriorInspectionRemainsUsable()
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        cancellation.CancelAfter(TimeSpan.FromSeconds(5));
        await using var staging = await project.StageAsync(cancellation.Token).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, cancellation.Token).ConfigureAwait(false);
        using var reader = project.Baseline.Retain();
        using var accepted = await transaction.PublishAsync(preview: null, project.Baseline, static () => { }, cancellation.Token).WaitAsync(cancellation.Token).ConfigureAwait(false);
        var oldRoot = reader.FindProjectRoot("Content")!;
        _ = (reader.PublicationId == accepted.PublicationId).Should().BeFalse();
        using var catalog = await reader.CreateCatalogAsync(reader.Roots.Single(static root => root.Name == "Content"), cancellation.Token).ConfigureAwait(false);
        _ = File.ReadAllText(Path.Combine(oldRoot, "value.txt")).Should().Be("old:Content");
        using var claimed = CookedGeneration.TryClaim(oldRoot);
        _ = claimed.Should().BeNull();
        project.AssertNew();
    }
}
