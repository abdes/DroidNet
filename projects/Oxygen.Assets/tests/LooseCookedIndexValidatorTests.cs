// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using Oxygen.Assets.Validation.LooseCooked.V1;
using Oxygen.Storage.Native;
using Testably.Abstractions.Testing;

namespace Oxygen.Assets.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public sealed class LooseCookedIndexValidatorTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task ValidateAsync_WhenIndexMissing_ShouldReportIssue()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Cooked");

        var storage = new NativeStorageProvider(fs);
        var issues = await LooseCookedIndexValidator.ValidateAsync(
            storage,
            cookedRootFolderPath: @"C:\Cooked",
            verifyFileRecords: true,
            verifyAssetDescriptors: true,
            this.TestContext.CancellationToken).ConfigureAwait(false);

        issues.Should().ContainSingle(i => i.Code == "index.missing");
    }

    [TestMethod]
    public async Task ValidateAsync_WhenHashesAndSizesMatch_ShouldReturnEmpty()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Cooked\assets");
        _ = fs.Directory.CreateDirectory(@"C:\Cooked\resources");

        var descriptorBytes = "descriptor"u8.ToArray();
        var fileBytes = "file"u8.ToArray();

        await fs.File.WriteAllBytesAsync(@"C:\Cooked\assets\A.asset", descriptorBytes, this.TestContext.CancellationToken).ConfigureAwait(true);
        await fs.File.WriteAllBytesAsync(@"C:\Cooked\resources\buffers.table", fileBytes, this.TestContext.CancellationToken).ConfigureAwait(true);

        var doc = new Document(
            ContentVersion: 1,
            Flags: IndexFeatures.HasVirtualPaths | IndexFeatures.HasFileRecords,
            Assets:
            [
                new AssetEntry(
                    AssetKey: new AssetKey(1, 2),
                    DescriptorRelativePath: "assets/A.asset",
                    VirtualPath: "/Content/A.asset",
                    AssetType: 1,
                    DescriptorSize: (ulong)descriptorBytes.Length,
                    DescriptorSha256: LooseCookedIndex.ComputeSha256(descriptorBytes)),
            ],
            Files:
            [
                new FileRecord(
                    Kind: FileKind.BuffersTable,
                    RelativePath: "resources/buffers.table",
                    Size: (ulong)fileBytes.Length,
                    Sha256: LooseCookedIndex.ComputeSha256(fileBytes)),
            ]);

        using (var ms = new MemoryStream())
        {
            LooseCookedIndex.Write(ms, doc);
            await fs.File.WriteAllBytesAsync(@"C:\Cooked\container.index.bin", ms.ToArray(), this.TestContext.CancellationToken).ConfigureAwait(true);
        }

        var storage = new NativeStorageProvider(fs);
        var issues = await LooseCookedIndexValidator.ValidateAsync(
            storage,
            cookedRootFolderPath: @"C:\Cooked",
            verifyFileRecords: true,
            verifyAssetDescriptors: true,
            this.TestContext.CancellationToken).ConfigureAwait(false);

        issues.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ValidateAsync_WhenDescriptorMissing_ShouldReportIssue()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Cooked");

        var doc = new Document(
            ContentVersion: 1,
            Flags: IndexFeatures.HasVirtualPaths,
            Assets:
            [
                new AssetEntry(
                    AssetKey: new AssetKey(1, 2),
                    DescriptorRelativePath: "assets/A.asset",
                    VirtualPath: "/Content/A.asset",
                    AssetType: 1,
                    DescriptorSize: 1,
                    DescriptorSha256: new byte[LooseCookedIndex.Sha256Size]),
            ],
            Files: []);

        using (var ms = new MemoryStream())
        {
            LooseCookedIndex.Write(ms, doc);
            await fs.File.WriteAllBytesAsync(@"C:\Cooked\container.index.bin", ms.ToArray(), this.TestContext.CancellationToken).ConfigureAwait(true);
        }

        var storage = new NativeStorageProvider(fs);
        var issues = await LooseCookedIndexValidator.ValidateAsync(
            storage,
            cookedRootFolderPath: @"C:\Cooked",
            verifyFileRecords: false,
            verifyAssetDescriptors: true,
            this.TestContext.CancellationToken).ConfigureAwait(false);

        issues.Should().ContainSingle(i => i.Code == "asset.descriptorMissing");
    }

    [TestMethod]
    public async Task ValidateAsync_WhenDescriptorShaMismatch_ShouldReportIssue()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Cooked\assets");

        var descriptorBytes = "descriptor"u8.ToArray();
        await fs.File.WriteAllBytesAsync(@"C:\Cooked\assets\A.asset", descriptorBytes, this.TestContext.CancellationToken).ConfigureAwait(true);

        var doc = new Document(
            ContentVersion: 1,
            Flags: IndexFeatures.HasVirtualPaths,
            Assets:
            [
                new AssetEntry(
                    AssetKey: new AssetKey(1, 2),
                    DescriptorRelativePath: "assets/A.asset",
                    VirtualPath: "/Content/A.asset",
                    AssetType: 1,
                    DescriptorSize: (ulong)descriptorBytes.Length,
                    DescriptorSha256: new byte[LooseCookedIndex.Sha256Size]),
            ],
            Files: []);

        using (var ms = new MemoryStream())
        {
            LooseCookedIndex.Write(ms, doc);
            await fs.File.WriteAllBytesAsync(@"C:\Cooked\container.index.bin", ms.ToArray(), this.TestContext.CancellationToken).ConfigureAwait(true);
        }

        var storage = new NativeStorageProvider(fs);
        var issues = await LooseCookedIndexValidator.ValidateAsync(
            storage,
            cookedRootFolderPath: @"C:\Cooked",
            verifyFileRecords: false,
            verifyAssetDescriptors: true,
            this.TestContext.CancellationToken).ConfigureAwait(false);

        issues.Should().ContainSingle(i => i.Code == "asset.descriptorSha256");
    }

    [TestMethod]
    public async Task ValidateAsync_WhenFileRecordMissing_ShouldReportIssue()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Cooked");

        var doc = new Document(
            ContentVersion: 1,
            Flags: IndexFeatures.HasVirtualPaths | IndexFeatures.HasFileRecords,
            Assets: [],
            Files:
            [
                new FileRecord(
                    Kind: FileKind.BuffersTable,
                    RelativePath: "resources/buffers.table",
                    Size: 1,
                    Sha256: new byte[LooseCookedIndex.Sha256Size]),
            ]);

        using (var ms = new MemoryStream())
        {
            LooseCookedIndex.Write(ms, doc);
            await fs.File.WriteAllBytesAsync(@"C:\Cooked\container.index.bin", ms.ToArray(), this.TestContext.CancellationToken).ConfigureAwait(true);
        }

        var storage = new NativeStorageProvider(fs);
        var issues = await LooseCookedIndexValidator.ValidateAsync(
            storage,
            cookedRootFolderPath: @"C:\Cooked",
            verifyFileRecords: true,
            verifyAssetDescriptors: false,
            this.TestContext.CancellationToken).ConfigureAwait(false);

        issues.Should().ContainSingle(i => i.Code == "file.missing");
    }
}
