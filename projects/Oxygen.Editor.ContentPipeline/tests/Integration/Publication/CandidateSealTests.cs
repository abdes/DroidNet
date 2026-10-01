// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Testing;
using static Oxygen.Editor.ContentPipeline.TestSupport.GenerationScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

[TestClass]
public sealed class CandidateSealTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>A new write explicitly retires the preceding proof, and sealing permanently ends writes.</summary>
    /// <returns>The asynchronous staged-output ownership regression.</returns>
    [TestMethod]
    public async Task CandidateRootRetainsFinalProofAndRejectsWritesAfterSealing()
    {
        using var project = new ProjectDirectory();
        var key = Guid.CreateVersion7();
        NativeInventoryFixture.WriteIndex(project.Root, [], key);
        var candidate = new CookStagingRoot("Content", key, project.Root);
        await using var candidateLifetime = candidate.ConfigureAwait(false);
        var api = NativeInventoryFixture.CreateApi();
        var first = await CookOutputReadLease.AcquireAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var firstInventory = await first.ReadInventoryAsync(api, this.TestContext.CancellationToken).ConfigureAwait(false);
        candidate.AcceptVerification(first, firstInventory);
        var index = Path.Combine(project.Root, "container.index.bin");
        Action write = () => new FileStream(index, FileMode.Open, FileAccess.Write, FileShare.None).Dispose();
        _ = write.Should().Throw<IOException>();
        await candidate.PrepareWriteAsync().ConfigureAwait(false);
        write();
        Action sealWithoutProof = () => candidate.Seal();
        _ = sealWithoutProof.Should().Throw<InvalidOperationException>();

        var final = await CookOutputReadLease.AcquireAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var inventory = await final.ReadInventoryAsync(api, this.TestContext.CancellationToken).ConfigureAwait(false);
        candidate.AcceptVerification(final, inventory);
        var binding = candidate.Seal();
        _ = binding.SourceKey.Should().Be(key);
        _ = binding.IndexSha256.Should().Be(inventory.IndexSha256);
        _ = candidate.Inventory.Should().BeSameAs(inventory);
        _ = write.Should().Throw<IOException>();
        Func<Task> reopen = candidate.PrepareWriteAsync;
        _ = await reopen.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
        _ = ReclaimGeneration(project.Root).Should().BeFalse();
    }
}
