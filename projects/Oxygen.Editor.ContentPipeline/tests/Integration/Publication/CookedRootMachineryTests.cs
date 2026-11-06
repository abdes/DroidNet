// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Assets.Testing;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

/// <summary>
/// A root holds more than its index lists: a writer's lock and its readers' leases, files earlier commits listed, and
/// bytes past a data file's committed length. None of them is a change to the content.
/// </summary>
[TestClass]
public sealed class CookedRootMachineryTests
{
    private const ulong TableSize = 8;
    private const ulong DataSize = 16;

    private readonly List<string> folders = [];

    public TestContext TestContext { get; set; } = null!;

    [TestCleanup]
    public void DeleteFolders()
    {
        foreach (var folder in this.folders.Where(Directory.Exists))
        {
            Directory.Delete(folder, recursive: true);
        }
    }

    [TestMethod]
    public async Task IndexMetadataAcceptsTheRootsOwnFilesAndWhatEarlierCommitsListed()
    {
        var snapshot = await this.ReadSnapshotAsync().ConfigureAwait(false);

        var validate = () => snapshot.ValidateMetadata(
        [
            new(LooseCookedIndex.FileName, 256),
            new(LooseCookedIndex.WriterLockFileName, 0),
            new(".leases/3.lock", 0),
            new("resources/Old.otex", 73),
            new("resources/buffers.table", TableSize),
            new("resources/buffers.data", DataSize),
        ]);

        _ = validate.Should().NotThrow();
    }

    [TestMethod]
    public async Task IndexMetadataDoesNotNeedARetiredFile()
    {
        var snapshot = await this.ReadSnapshotAsync().ConfigureAwait(false);

        var validate = () => snapshot.ValidateMetadata([new("resources/buffers.table", TableSize), new("resources/buffers.data", DataSize)]);

        _ = validate.Should().NotThrow();
    }

    [TestMethod]
    public async Task IndexMetadataAcceptsBytesPastADataFilesCommittedLength()
    {
        var snapshot = await this.ReadSnapshotAsync().ConfigureAwait(false);

        var validate = () => snapshot.ValidateMetadata([new("resources/buffers.table", TableSize), new("resources/buffers.data", DataSize + 1)]);

        _ = validate.Should().NotThrow();
    }

    [TestMethod]
    [DataRow(TableSize + 1, DataSize, DisplayName = "A table of another size")]
    [DataRow(TableSize, DataSize - 1, DisplayName = "A data file shorter than committed")]
    public async Task IndexMetadataRefusesAFileOfTheWrongSize(ulong tableSize, ulong dataSize)
    {
        var snapshot = await this.ReadSnapshotAsync().ConfigureAwait(false);

        var validate = () => snapshot.ValidateMetadata([new("resources/buffers.table", tableSize), new("resources/buffers.data", dataSize)]);

        _ = validate.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public async Task IndexMetadataRefusesAFileNoCommitLists()
    {
        var snapshot = await this.ReadSnapshotAsync().ConfigureAwait(false);

        var validate = () => snapshot.ValidateMetadata(
        [
            new("resources/buffers.table", TableSize),
            new("resources/buffers.data", DataSize),
            new("resources/Stray.bin", 1),
        ]);

        _ = validate.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public async Task ARootsImageAndCopyLeaveOutTheWritersLockAndTheReadersLeases()
    {
        var root = this.NewFolder();
        var copy = this.NewFolder();
        Directory.Delete(copy);
        await this.WriteAsync(root, "resources/buffers.table").ConfigureAwait(false);
        await this.WriteAsync(root, LooseCookedIndex.WriterLockFileName).ConfigureAwait(false);
        await this.WriteAsync(root, ".leases/3.lock").ConfigureAwait(false);

        var image = await CookRootImage.CaptureAsync(root, copy, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = image.Files.Keys.Should().Equal("resources/buffers.table");
        _ = File.Exists(Path.Combine(copy, LooseCookedIndex.WriterLockFileName)).Should().BeFalse();
        _ = Directory.Exists(Path.Combine(copy, LooseCookedIndex.LeasesFolderName)).Should().BeFalse();
        _ = File.Exists(Path.Combine(copy, "resources", "buffers.table")).Should().BeTrue();
    }

    [TestMethod]
    public async Task AReadersLeaseIsNoChangeToTheRoot()
    {
        var root = this.NewFolder();
        await this.WriteAsync(root, "resources/buffers.table").ConfigureAwait(false);
        var before = await CookRootImage.CaptureAsync(root, copyTo: null, this.TestContext.CancellationToken).ConfigureAwait(false);

        await this.WriteAsync(root, ".leases/7.lock").ConfigureAwait(false);
        var after = await CookRootImage.CaptureAsync(root, copyTo: null, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = after.Matches(before).Should().BeTrue();
    }

    private async Task<CookedIndexSnapshot> ReadSnapshotAsync()
    {
        var root = this.NewFolder();
        var document = new Document(
            1,
            IndexFeatures.HasVirtualPaths | IndexFeatures.HasFileRecords,
            Guid.CreateVersion7(),
            [],
            [
                new FileRecord(FileKind.BuffersTable, "resources/buffers.table", TableSize, SHA256.HashData("table"u8)),
                new FileRecord(FileKind.BuffersData, "resources/buffers.data", DataSize, new byte[LooseCookedIndex.Sha256Size]),
            ])
        {
            Commit = 4,
            Retired = [new RetiredFile(FileKind.TextureDescriptor, "resources/Old.otex", 3)],
        };
        var index = File.Create(Path.Combine(root, LooseCookedIndex.FileName));
        await using (index.ConfigureAwait(false))
        {
            LooseCookedIndexFixture.Write(index, document);
        }

        var read = await CookedIndexSnapshot.ReadAsync(root, this.TestContext.CancellationToken).ConfigureAwait(false);
        return read.Should().BeOfType<CookedIndexRead.Current>().Subject.Snapshot;
    }

    private async Task WriteAsync(string root, string relative)
    {
        var path = Path.Combine(root, relative);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        await File.WriteAllTextAsync(path, relative, this.TestContext.CancellationToken).ConfigureAwait(false);
    }

    private string NewFolder()
    {
        var folder = Directory.CreateTempSubdirectory("oxygen-root-machinery-").FullName;
        this.folders.Add(folder);
        return folder;
    }
}
