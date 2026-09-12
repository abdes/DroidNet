// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Managed.Core.Tests;

/// <summary>Verifies exact fixed-receipt matching and artifact ownership before native loading.</summary>
[TestClass]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class NativeCompatibilityVerifierTests
{
    /// <summary>Gets or sets the current test cancellation context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Both supported build configurations require and preserve an exact accepted file set.</summary>
    /// <param name="configuration">The compatible build configuration.</param>
    /// <returns>The asynchronous verification test.</returns>
    [TestMethod]
    [DataRow("Debug")]
    [DataRow("Release")]
    [SuppressMessage("Reliability", "CA1849:Call async methods when in an async method", Justification = "Exercises the synchronous disposal contract after asynchronous disposal to verify idempotence across both APIs.")]
    [SuppressMessage("Usage", "MA0042:Do not use blocking calls in an async method", Justification = "Exercises the synchronous disposal contract after asynchronous disposal to verify idempotence across both APIs.")]
    public async Task MatchingFilesAreLeasedUntilNativeOwnershipEnds(string configuration)
    {
        using var fixture = new Fixture(configuration);
        var receiptBytes = await File.ReadAllBytesAsync(fixture.ReceiptPath, this.TestContext.CancellationToken).ConfigureAwait(false);
        var result = await fixture.VerifyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Diagnostics.Should().BeEmpty();
        _ = result.Artifacts.Should().NotBeNull();
        var lease = result.Artifacts!;
        await using var leaseLifetime = lease.ConfigureAwait(false);
        var path = lease.GetPath(fixture.Locations[0].Id);
        _ = path.Should().Be(fixture.Locations[0].FullPath);
        Action overwrite = () => File.WriteAllText(path, "uncompatible replacement");
        _ = overwrite.Should().Throw<IOException>();
        Action replace = () => File.Move(path, path + ".old");
        _ = replace.Should().Throw<IOException>();
        await lease.DisposeAsync().ConfigureAwait(false);
        lease.Dispose();
        _ = overwrite.Should().NotThrow();
        Action useReleased = () => _ = lease.GetPath(fixture.Locations[0].Id);
        _ = useReleased.Should().Throw<ObjectDisposedException>();
        _ = (await File.ReadAllBytesAsync(fixture.ReceiptPath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(receiptBytes);
    }

    /// <summary>Content changes cannot be hidden by preserving file length and timestamp.</summary>
    /// <returns>The asynchronous verification test.</returns>
    [TestMethod]
    public async Task SameSizeAndTimestampChangeFailsWithoutRefreshingReceipt()
    {
        using var fixture = new Fixture();
        var path = fixture.Locations[0].FullPath;
        var stamp = File.GetLastWriteTimeUtc(path);
        await File.WriteAllTextAsync(path, "WXYZ", this.TestContext.CancellationToken).ConfigureAwait(false);
        File.SetLastWriteTimeUtc(path, stamp);
        var receipt = await File.ReadAllBytesAsync(fixture.ReceiptPath, this.TestContext.CancellationToken).ConfigureAwait(false);

        var result = await fixture.VerifyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Artifacts.Should().BeNull();
        _ = result.Diagnostics.Should().ContainSingle(diagnostic => diagnostic.Code == NativeCompatibilityDiagnosticCodes.ArtifactMismatch && diagnostic.AffectedPath == path);
        _ = (await File.ReadAllBytesAsync(fixture.ReceiptPath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(receipt);
        await File.WriteAllTextAsync(fixture.Locations[1].FullPath, "all verification handles released", this.TestContext.CancellationToken).ConfigureAwait(false);
    }

    /// <summary>Missing and changed files are reported together and no partial set can be used.</summary>
    /// <returns>The asynchronous verification test.</returns>
    [TestMethod]
    public async Task MultipleArtifactFailuresReleaseTheWholeSet()
    {
        using var fixture = new Fixture();
        File.Delete(fixture.Locations[1].FullPath);
        await File.WriteAllTextAsync(fixture.Locations[2].FullPath, "changed schema", this.TestContext.CancellationToken).ConfigureAwait(false);

        var result = await fixture.VerifyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Artifacts.Should().BeNull();
        _ = result.Diagnostics.Should().HaveCount(2).And.OnlyContain(diagnostic => diagnostic.OperationId == fixture.OperationId);
        await File.WriteAllTextAsync(fixture.Locations[0].FullPath, "released", this.TestContext.CancellationToken).ConfigureAwait(false);
    }

    /// <summary>A receipt for another configuration never qualifies the running build.</summary>
    /// <returns>The asynchronous verification test.</returns>
    [TestMethod]
    public async Task ReleaseReceiptCannotQualifyDebug()
    {
        using var fixture = new Fixture("Release");
        var result = await NativeCompatibilityVerifier.VerifyAsync(fixture.OperationId, fixture.ReceiptPath, "Debug", fixture.Locations, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic => diagnostic.Code == NativeCompatibilityDiagnosticCodes.ConfigurationMismatch);
        _ = result.Artifacts.Should().BeNull();
    }

    /// <summary>Compatibility must cover exactly the independently required inventory.</summary>
    /// <param name="missingFromReceipt">Whether to omit an installed artifact from the accepted receipt.</param>
    /// <returns>The asynchronous verification test.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task RequiredArtifactSetCannotBeSilentlyChanged(bool missingFromReceipt)
    {
        using var fixture = new Fixture();
        if (missingFromReceipt)
        {
            fixture.WriteReceipt(fixture.Receipt with { Artifacts = fixture.Receipt.Artifacts.RemoveAt(0) });
        }
        else
        {
            fixture.Locations.RemoveAt(0);
        }

        var result = await fixture.VerifyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Artifacts.Should().BeNull();
        _ = result.Diagnostics.Should().ContainSingle(diagnostic => diagnostic.Code == NativeCompatibilityDiagnosticCodes.ArtifactSetMismatch);
    }

    /// <summary>Schema identifiers are part of the accepted producer contract even when bytes match.</summary>
    /// <returns>The asynchronous verification test.</returns>
    [TestMethod]
    public async Task RequiredSchemaVersionMustMatchCompatibility()
    {
        using var fixture = new Fixture();
        fixture.Locations[2] = fixture.Locations[2] with { SchemaId = "oxygen.test.v2" };
        var result = await fixture.VerifyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Artifacts.Should().BeNull();
        _ = result.Diagnostics.Should().ContainSingle(diagnostic => diagnostic.Code == NativeCompatibilityDiagnosticCodes.ArtifactMismatch && diagnostic.Message.Contains("schema", StringComparison.Ordinal));
    }

    /// <summary>Installation relocation, record order and report revision do not change producer content identity.</summary>
    /// <returns>The asynchronous verification test.</returns>
    [TestMethod]
    [SuppressMessage("Globalization", "CA1308:Normalize strings to uppercase", Justification = "Intentionally tests lowercase receipt hashes against canonical producer fingerprints.")]
    public async Task FingerprintIsPortableAndIndependentOfReceiptOrdering()
    {
        using var first = new Fixture();
        using var second = new Fixture();
        second.WriteReceipt(second.Receipt with { Artifacts = [.. second.Receipt.Artifacts.Reverse().Select(static artifact => artifact with { Sha256 = artifact.Sha256.ToLowerInvariant() })] });
        var firstResult = await first.VerifyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var secondResult = await second.VerifyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var firstLease = firstResult.Artifacts!;
        await using var firstLifetime = firstLease.ConfigureAwait(false);
        var secondLease = secondResult.Artifacts!;
        await using var secondLifetime = secondLease.ConfigureAwait(false);
        _ = firstResult.Succeeded.Should().BeTrue();
        _ = secondResult.Succeeded.Should().BeTrue();
        _ = firstLease.Fingerprint.Should().Be(secondLease.Fingerprint);
        _ = firstLease.GetPath(first.Locations[0].Id).Should().NotBe(secondLease.GetPath(second.Locations[0].Id));
    }

    /// <summary>A newly compatible producer or schema gets a distinct cook provenance identity.</summary>
    /// <param name="schemaChange">Whether to change the schema contract instead of artifact bytes.</param>
    /// <returns>The asynchronous verification test.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task FingerprintChangesWithProducerOrSchema(bool schemaChange)
    {
        using var fixture = new Fixture();
        var original = await fixture.VerifyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = original.Succeeded.Should().BeTrue();
        var fingerprint = original.Artifacts!.Fingerprint;
        await original.Artifacts.DisposeAsync().ConfigureAwait(false);
        var artifacts = fixture.Receipt.Artifacts;
        if (schemaChange)
        {
            fixture.Locations[2] = fixture.Locations[2] with { SchemaId = "oxygen.test.v2" };
            artifacts = artifacts.SetItem(2, artifacts[2] with { SchemaId = "oxygen.test.v2" });
        }
        else
        {
            var bytes = Encoding.UTF8.GetBytes("changed producer");
            await File.WriteAllBytesAsync(fixture.Locations[0].FullPath, bytes, this.TestContext.CancellationToken).ConfigureAwait(false);
            artifacts = artifacts.SetItem(0, artifacts[0] with { Size = bytes.Length, Sha256 = Convert.ToHexString(SHA256.HashData(bytes)) });
        }

        fixture.WriteReceipt(fixture.Receipt with { Artifacts = artifacts });
        var changed = await fixture.VerifyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var lease = changed.Artifacts!;
        await using var lifetime = lease.ConfigureAwait(false);
        _ = changed.Succeeded.Should().BeTrue();
        _ = lease.Fingerprint.Should().NotBe(fingerprint);
    }

    /// <summary>Cancellation after checking a file releases its handle and cannot return a compatible lease.</summary>
    /// <returns>The asynchronous verification test.</returns>
    [TestMethod]
    public async Task CancellationDuringVerificationReleasesAlreadyCheckedFiles()
    {
        using var fixture = new Fixture();
        using var cancellation = new CancellationTokenSource();
        var progress = new InlineProgress(value =>
        {
            _ = value.Checked.Should().Be(1);
            cancellation.Cancel();
        });
        Func<Task> verify = () => NativeCompatibilityVerifier.VerifyAsync(fixture.OperationId, fixture.ReceiptPath, "Debug", fixture.Locations, cancellation.Token, progress);
        _ = await verify.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        foreach (var artifact in fixture.Locations)
        {
            await File.WriteAllTextAsync(artifact.FullPath, "released", this.TestContext.CancellationToken).ConfigureAwait(false);
        }
    }

    /// <summary>The verifier snapshots caller inventory before awaiting file reads.</summary>
    /// <returns>The asynchronous verification test.</returns>
    [TestMethod]
    public async Task CallerMutationCannotChangeInventoryDuringVerification()
    {
        using var fixture = new Fixture();
        var expected = fixture.Locations.ToArray();
        var progress = new InlineProgress(_ => fixture.Locations.Clear());
        var result = await NativeCompatibilityVerifier.VerifyAsync(fixture.OperationId, fixture.ReceiptPath, "Debug", fixture.Locations, this.TestContext.CancellationToken, progress).ConfigureAwait(false);
        var lease = result.Artifacts!;
        await using var lifetime = lease.ConfigureAwait(false);
        _ = result.Succeeded.Should().BeTrue();
        foreach (var artifact in expected)
        {
            _ = lease.GetPath(artifact.Id).Should().Be(artifact.FullPath);
        }
    }

    /// <summary>Missing or malformed build receipts cannot be repaired implicitly by verification.</summary>
    /// <param name="problem">The malformed receipt case.</param>
    /// <returns>The asynchronous verification test.</returns>
    [TestMethod]
    [DataRow("missing")]
    [DataRow("json")]
    [DataRow("version")]
    [DataRow("configuration")]
    [DataRow("empty")]
    [DataRow("duplicate")]
    [DataRow("hash")]
    [DataRow("size")]
    [DataRow("schema")]
    [DataRow("null")]
    [DataRow("unknown")]
    [DataRow("missingArtifacts")]
    [DataRow("nullHash")]
    [DataRow("repeatedProperty")]
    [DataRow("relativeId")]
    public async Task InvalidReceiptDoesNotProduceCompatibility(string problem)
    {
        using var fixture = new Fixture();
        fixture.CorruptReceipt(problem);
        var existed = File.Exists(fixture.ReceiptPath);
        var bytes = existed ? await File.ReadAllBytesAsync(fixture.ReceiptPath, this.TestContext.CancellationToken).ConfigureAwait(false) : [];

        var result = await fixture.VerifyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Artifacts.Should().BeNull();
        _ = result.Diagnostics.Should().ContainSingle(diagnostic => diagnostic.Code == NativeCompatibilityDiagnosticCodes.ReceiptInvalid);
        _ = File.Exists(fixture.ReceiptPath).Should().Be(existed);
        if (existed)
        {
            _ = (await File.ReadAllBytesAsync(fixture.ReceiptPath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(bytes);
        }
    }

    private sealed class InlineProgress(Action<NativeCompatibilityProgress> report) : IProgress<NativeCompatibilityProgress>
    {
        public void Report(NativeCompatibilityProgress value) => report(value);
    }

    private sealed class Fixture : IDisposable
    {
        private static readonly JsonSerializerOptions Options = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };

        public Fixture(string configuration = "Debug")
        {
            this.Configuration = configuration;
            Directory.CreateDirectory(this.Root);
            var artifacts = new List<NativeArtifact>();
            foreach (var (id, contents, schema) in new[] { ("editor/editor.dll", "ABCD", (string?)null), ("engine/runtime.dll", "native runtime", null), ("engine/schema.json", "schema bytes", "oxygen.test.v1") })
            {
                var path = Path.GetFullPath(Path.Combine(this.Root, id));
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                var bytes = Encoding.UTF8.GetBytes(contents);
                File.WriteAllBytes(path, bytes);
                this.Locations.Add(new(id, path, schema));
                artifacts.Add(new(id, bytes.Length, Convert.ToHexString(SHA256.HashData(bytes)), schema));
            }

            this.Receipt = new(1, configuration, [.. artifacts]);
            this.WriteReceipt(this.Receipt);
        }

        public string Root { get; } = Path.Combine(Path.GetTempPath(), "OxygenCompatibilityTests", Guid.NewGuid().ToString("N"));

        public string Configuration { get; }

        public Guid OperationId { get; } = Guid.NewGuid();

        public string ReceiptPath => Path.Combine(this.Root, "compatibility.json");

        public NativeBuildReceipt Receipt { get; }

        public List<NativeArtifactLocation> Locations { get; } = [];

        public Task<NativeCompatibilityResult> VerifyAsync(CancellationToken cancellationToken)
            => NativeCompatibilityVerifier.VerifyAsync(this.OperationId, this.ReceiptPath, this.Configuration, this.Locations, cancellationToken);

        public void WriteReceipt(NativeBuildReceipt receipt) => File.WriteAllText(this.ReceiptPath, JsonSerializer.Serialize(receipt, Options));

        public void CorruptReceipt(string problem)
        {
            var root = JsonNode.Parse(File.ReadAllText(this.ReceiptPath))!;
            switch (problem)
            {
                case "missing": File.Delete(this.ReceiptPath); return;
                case "json": File.WriteAllText(this.ReceiptPath, "not json"); return;
                case "version": root["version"] = 2; break;
                case "configuration": root["configuration"] = "Checked"; break;
                case "revision": root["sourceRevision"] = "main"; break;
                case "empty": root["artifacts"] = new JsonArray(); break;
                case "duplicate": root["artifacts"]!.AsArray().Add(root["artifacts"]![0]!.DeepClone()); break;
                case "hash": root["artifacts"]![0]!["sha256"] = new string('z', 64); break;
                case "size": root["artifacts"]![0]!["size"] = -1; break;
                case "schema": root["artifacts"]![2]!["schemaId"] = string.Empty; break;
                case "null": root["artifacts"]!.AsArray().Add(null); break;
                case "unknown": root["autoRefresh"] = true; break;
                case "missingArtifacts": _ = root.AsObject().Remove("artifacts"); break;
                case "nullHash": root["artifacts"]![0]!["sha256"] = null; break;
                case "nullRevision": root["sourceRevision"] = null; break;
                case "repeatedProperty": File.WriteAllText(this.ReceiptPath, "{\"version\":1," + root.ToJsonString()[1..]); return;
                case "relativeId": root["artifacts"]![0]!["id"] = "engine/../other.dll"; break;
            }

            File.WriteAllText(this.ReceiptPath, root.ToJsonString());
        }

        public void Dispose() => Directory.Delete(this.Root, recursive: true);
    }
}
