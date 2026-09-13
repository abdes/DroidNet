// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Qualifies coordinated read ownership and registration lifetimes.</summary>
[TestClass]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed partial class CookDocumentRegistryTests
{
    /// <summary>Acquires all owners in stable order and releases each exactly once in reverse order.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ReadSetOrdersMultipleOwnersAndReleasesExactlyOnce()
    {
        var registry = new CookDocumentRegistry();
        var firstPath = Path.Combine(Path.GetTempPath(), "a.omat.json");
        var secondPath = Path.Combine(Path.GetTempPath(), "b.omat.json");
        List<string> calls = [];
        using var second = registry.Register(secondPath, _ => Acquire("second", secondPath));
        using var first = registry.Register(firstPath, _ => Acquire("first", firstPath));
        using var alias = registry.Register(firstPath, _ => Acquire("alias", firstPath));
        using var closed = registry.Register(firstPath, _ => Task.FromResult<CookDocumentReadLease?>(null));
        using var reads = await registry.AcquireAsync([secondPath, firstPath, firstPath], CancellationToken.None).ConfigureAwait(false);

        _ = reads.Documents.Should().HaveCount(3);
        _ = calls.Should().Equal("read first", "read alias", "read second");
        reads.Dispose();
        reads.Dispose();
        _ = calls.Should().Equal("read first", "read alias", "read second", "release second", "release alias", "release first");

        Task<CookDocumentReadLease?> Acquire(string name, string path)
        {
            calls.Add("read " + name);
            return Task.FromResult<CookDocumentReadLease?>(new(State(path), () => calls.Add("release " + name)));
        }
    }

    /// <summary>Releases earlier document gates when cancellation interrupts a later acquisition.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CancelledAcquisitionReleasesEarlierReads()
    {
        var registry = new CookDocumentRegistry();
        var firstPath = Path.Combine(Path.GetTempPath(), "a.omat.json");
        var secondPath = Path.Combine(Path.GetTempPath(), "b.omat.json");
        using var cancellation = new CancellationTokenSource();
        var released = false;
        using var first = registry.Register(
            firstPath,
            _ => Task.FromResult<CookDocumentReadLease?>(new(State(firstPath), () => released = true)));
        using var second = registry.Register(secondPath, async token =>
        {
            await cancellation.CancelAsync().ConfigureAwait(false);
            token.ThrowIfCancellationRequested();
            return null;
        });

        var acquire = () => registry.AcquireAsync([firstPath, secondPath], cancellation.Token);
        _ = await acquire.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);

        _ = released.Should().BeTrue();
    }

    /// <summary>Retires a closed document registration without affecting another owner of the same source.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ClosedRegistrationDoesNotAcquireAgain()
    {
        var registry = new CookDocumentRegistry();
        var path = Path.Combine(Path.GetTempPath(), "material.omat.json");
        var acquired = false;
        using var closed = registry.Register(path, _ =>
        {
            acquired = true;
            return Task.FromResult<CookDocumentReadLease?>(new(State(path), () => { }));
        });
        using var open = registry.Register(path, _ => Task.FromResult<CookDocumentReadLease?>(new(State(path), () => { })));
        closed.Dispose();

        using var reads = await registry.AcquireAsync([path], CancellationToken.None).ConfigureAwait(false);

        _ = acquired.Should().BeFalse();
        _ = reads.Documents.Should().ContainSingle();
    }

    private static CookDocumentState State(string path) => new(Guid.NewGuid(), path, "Material", 1, 1, IsDirty: false, "saved hash");
}
