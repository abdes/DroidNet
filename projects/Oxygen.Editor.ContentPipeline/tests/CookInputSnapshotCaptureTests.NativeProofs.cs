// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Import;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Qualifies native range and metadata proofs independently of a later whole-file hash.</summary>
public sealed partial class CookInputSnapshotCaptureTests
{
    /// <summary>Directory timestamps do not dirty reproducible inputs, while fresh native observations still require exact coherence.</summary>
    /// <returns>The freshness and capture verification.</returns>
    [TestMethod]
    public async Task DirectoryTimestampChangesKeepFreshnessButRecheckNativeProofs()
    {
        using var workspace = new CaptureWorkspace();
        var directory = Path.Combine(workspace.Root, "Content", "images");
        _ = Directory.CreateDirectory(directory);
        var metadata = CookSavedSourceReader.ReadMetadata(directory);
        var accepted = new Snapshots.CookSnapshotInput(null, directory, "Content/images", string.Empty, Snapshots.CookSnapshotInputKind.Probe)
        {
            Metadata = metadata,
        };
        Directory.SetLastWriteTimeUtc(directory, DateTime.UtcNow.AddMinutes(-5));
        _ = CookSavedSourceReader.MatchesProbe(accepted).Should().BeTrue();
        var current = accepted with { Metadata = CookSavedSourceReader.ReadMetadata(directory) };
        _ = Snapshots.CookDependencyDiscovery.FingerprintImportedContent([current])
            .Should().Be(Snapshots.CookDependencyDiscovery.FingerprintImportedContent([accepted]));

        var discoveries = 0;
        var result = await workspace.CaptureAsync(_ =>
        {
            discoveries++;
            return Task.FromResult<IReadOnlyList<Snapshots.CookSnapshotInput>>(
                [current with { NativeObservations = [new(directory, true, discoveries == 1 ? metadata : current.Metadata, [])] }]);
        }).ConfigureAwait(false);
        _ = discoveries.Should().Be(2);
        _ = result.Snapshot.Should().NotBeNull();
        Directory.Delete(directory);
        _ = CookSavedSourceReader.MatchesProbe(accepted).Should().BeFalse();
        await File.WriteAllTextAsync(directory, string.Empty).ConfigureAwait(false);
        _ = CookSavedSourceReader.MatchesProbe(accepted).Should().BeFalse();
    }

    /// <summary>An abandoned cubemap candidate retains its probe without copying directory contents.</summary>
    /// <returns>The asynchronous capture test.</returns>
    [TestMethod]
    public async Task PositiveDirectoryProbeCreatesNoCapturedFile()
    {
        using var workspace = new CaptureWorkspace();
        var path = Path.Combine(workspace.Root, "Content", "sky_px.png");
        _ = Directory.CreateDirectory(path);
        var input = new Snapshots.CookSnapshotInput(null, path, "Content/sky_px.png", string.Empty, Snapshots.CookSnapshotInputKind.Probe)
        {
            NativeObservations = [new(path, true, null, [])],
        };
        var snapshot = (await workspace.CaptureAsync([input]).ConfigureAwait(false)).Snapshot!;
        _ = Directory.EnumerateFileSystemEntries(snapshot.InputRoot).Should().BeEmpty();
        var captured = snapshot.CreateNativeInputs().Inputs.Single();
        _ = captured.Exists.Should().BeTrue();
        _ = captured.Metadata.Should().BeNull();
        _ = captured.File.Should().BeNull();
    }

    /// <summary>Directory metadata survives persisted source facts and native admission.</summary>
    /// <returns>The asynchronous capture test.</returns>
    [TestMethod]
    public async Task DirectoryMetadataProbeSurvivesPublicationSerialization()
    {
        using var workspace = new CaptureWorkspace();
        var path = Path.Combine(workspace.Root, "Content", "image.png");
        _ = Directory.CreateDirectory(path);
        var metadata = CookSavedSourceReader.ReadMetadata(path);
        var input = new Snapshots.CookSnapshotInput(null, path, "Content/image.png", string.Empty, Snapshots.CookSnapshotInputKind.Probe)
        {
            Metadata = metadata, NativeObservations = [new(path, true, metadata, [])],
        };
        var snapshot = (await workspace.CaptureAsync([input]).ConfigureAwait(false)).Snapshot!;
        var serialized = System.Text.Json.JsonSerializer.Serialize(snapshot.Inputs.Single());
        var retained = System.Text.Json.JsonSerializer.Deserialize<Snapshots.CookSnapshotInput>(serialized)!;
        _ = retained.Kind.Should().Be(Snapshots.CookSnapshotInputKind.Probe);
        _ = retained.Metadata.Should().Be(metadata);
        _ = retained.NativeObservations.Should().BeEmpty();
        var captured = snapshot.CreateNativeInputs().Inputs.Single();
        _ = captured.Metadata!.IsDirectory.Should().BeTrue();
        _ = captured.File.Should().BeNull();
    }

    /// <summary>Rechecks every consumed range even if managed discovery hashed the new bytes.</summary>
    /// <returns>The asynchronous capture test.</returns>
    [TestMethod]
    public async Task NativeRangeMismatchRetriesEvenWhenWholeFileHashMatches()
    {
        using var workspace = new CaptureWorkspace();
        var input = workspace.WriteInput("Content/source.bin", "prefixNEWsuffix");
        var discoveries = 0;
        var result = await workspace.CaptureAsync(_ =>
        {
            discoveries++;
            var observed = input with
            {
                NativeObservations =
                [
                    new(input.SourcePath, true, null, [new(0, 6, Hash("prefix"))]),
                    new(input.SourcePath, true, null, [new(6, 3, Hash(discoveries == 1 ? "OLD" : "NEW")), new(9, 0, Hash("suffix"))]),
                ],
            };
            return Task.FromResult<IReadOnlyList<Snapshots.CookSnapshotInput>>([observed]);
        }).ConfigureAwait(false);
        _ = discoveries.Should().Be(2);
        _ = result.Snapshot.Should().NotBeNull();
    }

    /// <summary>Bounds memory and clamps a ranged read at EOF exactly as the native reader does.</summary>
    /// <returns>The asynchronous capture test.</returns>
    [TestMethod]
    public async Task NativeRangesHandleLargeInputsAndEndOfFile()
    {
        using var workspace = new CaptureWorkspace();
        var content = new string('x', 200_000);
        var input = workspace.WriteInput("Content/source.bin", content);
        input = input with
        {
            NativeObservations = [new(
                input.SourcePath,
                true,
                null,
                [new(0, 0, Hash(content)), new(3, 150_000, Hash(new string('x', 150_000))),
                 new(199_999, ulong.MaxValue, Hash("x")), new(ulong.MaxValue, 0, Hash(string.Empty))])],
        };
        var result = await workspace.CaptureAsync([input]).ConfigureAwait(false);
        _ = result.Snapshot.Should().NotBeNull();
    }

    /// <summary>Source timestamps are part of observed input state, including dates before the Unix epoch.</summary>
    /// <returns>The asynchronous capture test.</returns>
    [TestMethod]
    public async Task NativeMetadataMismatchRetriesWithNanosecondPrecision()
    {
        using var workspace = new CaptureWorkspace();
        var input = workspace.WriteInput("Content/source.bin", "bytes");
        var timestamp = DateTimeOffset.FromUnixTimeSeconds(-1).AddTicks(1234567);
        File.SetLastWriteTimeUtc(input.SourcePath, timestamp.UtcDateTime);
        var discoveries = 0;
        var result = await workspace.CaptureAsync(_ =>
        {
            discoveries++;
            var metadata = new NativeSourceFileMetadata(5, false, false, -1, discoveries == 1 ? 123456800 : 123456700);
            return Task.FromResult<IReadOnlyList<Snapshots.CookSnapshotInput>>(
                [input with { NativeObservations = [new(input.SourcePath, true, metadata, [new(0, 0, input.DiscoveryHash)])] }]);
        }).ConfigureAwait(false);
        _ = discoveries.Should().Be(2);
        _ = result.Snapshot.Should().NotBeNull();
    }
}
