// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Generation ownership survives publication and excludes reclamation.</summary>
public sealed partial class CookOutputLeaseTests
{
    /// <summary>Both sharing protocols used by managed and native readers exclude reclamation.</summary>
    [TestMethod]
    public void GenerationReadersExcludeReclamationUntilAllClose()
    {
        using var project = new ProjectDirectory();
        var marker = Path.Combine(project.Root, ".generation.lock");
        File.WriteAllBytes(marker, []);
        using var managed = WindowsCookFile.OpenGenerationReader(marker);
        // Serio's shared reader uses Read access with Read/Write/Delete sharing.
        using var native = new FileStream(marker, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        _ = WindowsCookFile.TryClaimGeneration(marker).Should().BeNull();
        managed.Dispose();
        _ = WindowsCookFile.TryClaimGeneration(marker).Should().BeNull();
        native.Dispose();
        using var claimed = WindowsCookFile.TryClaimGeneration(marker);
        _ = claimed.Should().NotBeNull();
    }

    /// <summary>Index removal prevents admission if cleanup is interrupted before the directory is removed.</summary>
    [TestMethod]
    public void GenerationReclamationRemovesIndexBeforeDeletingPayloads()
    {
        using var project = new ProjectDirectory();
        var root = Path.Combine(project.Root, "generation");
        _ = Directory.CreateDirectory(root);
        var marker = Path.Combine(root, ".generation.lock");
        File.WriteAllBytes(marker, []);
        File.WriteAllText(Path.Combine(root, "container.index.bin"), "owned fixture");
        using var claimed = WindowsCookFile.TryClaimGeneration(marker);
        _ = claimed.Should().NotBeNull();
        Action read = () => WindowsCookFile.OpenGenerationReader(marker).Dispose();
        _ = read.Should().Throw<CookOutputBusyException>();
        Action nativeRead = () => new FileStream(marker, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete).Dispose();
        _ = nativeRead.Should().Throw<IOException>();
        File.Delete(Path.Combine(root, "container.index.bin"));
        Directory.Delete(root, recursive: true);
        _ = Directory.Exists(root).Should().BeFalse();
    }

    /// <summary>Sealing adds lifetime ownership without invalidating an unchanged verified content opening.</summary>
    /// <returns>The asynchronous seal regression.</returns>
    [TestMethod]
    public async Task SealKeepsVerifiedMembersAndRetainsGeneration()
    {
        using var project = new ProjectDirectory();
        await File.WriteAllTextAsync(Path.Combine(project.Root, "container.index.bin"), "index", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var content = await CookOutputReadLease.AcquireAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var before = content.GetFiles();
        var marker = Path.Combine(project.Root, ".generation.lock");
        await File.WriteAllBytesAsync(marker, [], this.TestContext.CancellationToken).ConfigureAwait(false);
        content.RetainGeneration();
        _ = content.GetFiles().Should().Equal(before);
        _ = WindowsCookFile.TryClaimGeneration(marker).Should().BeNull();
        await content.DisposeAsync().ConfigureAwait(false);
        using var reclaimed = WindowsCookFile.TryClaimGeneration(marker);
        _ = reclaimed.Should().NotBeNull();
    }

    /// <summary>A failed payload deletion retains the ownership marker for a safe retry.</summary>
    [TestMethod]
    public void GenerationCleanupRetainsMarkerAfterPayloadFailure()
    {
        using var project = new ProjectDirectory();
        var root = Path.Combine(project.Root, "generation");
        _ = Directory.CreateDirectory(root);
        var marker = Path.Combine(root, CookedGeneration.MarkerFileName);
        var index = Path.Combine(root, "container.index.bin");
        var payload = Path.Combine(root, "buffers.data");
        File.WriteAllBytes(marker, []);
        File.WriteAllText(index, "index");
        File.WriteAllText(payload, "payload");
        using var held = new FileStream(payload, FileMode.Open, FileAccess.Read, FileShare.Read);
        Action reclaim = () => ReclaimGeneration(root);
        _ = reclaim.Should().Throw<IOException>();
        _ = File.Exists(index).Should().BeFalse();
        _ = File.Exists(marker).Should().BeTrue();
        held.Dispose();
        _ = ReclaimGeneration(root).Should().BeTrue();
        _ = Directory.Exists(root).Should().BeFalse();
    }

    /// <summary>Maintenance skips readers and unfinished output but can remove an empty interrupted shell.</summary>
    [TestMethod]
    public void GenerationCleanupPreservesReadersAndUnsealedOutput()
    {
        using var project = new ProjectDirectory();
        var root = Path.Combine(project.Root, "generation");
        _ = Directory.CreateDirectory(root);
        var index = Path.Combine(root, "container.index.bin");
        File.WriteAllText(index, "index");
        _ = ReclaimGeneration(root).Should().BeFalse();
        var marker = Path.Combine(root, CookedGeneration.MarkerFileName);
        File.WriteAllBytes(marker, []);
        using var reader = WindowsCookFile.OpenGenerationReader(marker);
        _ = ReclaimGeneration(root).Should().BeFalse();
        _ = File.Exists(index).Should().BeTrue();
        reader.Dispose();
        _ = ReclaimGeneration(root).Should().BeTrue();
        _ = Directory.CreateDirectory(root);
        _ = ReclaimGeneration(root).Should().BeTrue();
    }
    private static bool ReclaimGeneration(string root)
    {
        using var generation = CookedGeneration.TryClaim(root);
        if (generation is null)
        {
            return !Directory.Exists(root);
        }

        generation.Delete();
        return true;
    }

}
