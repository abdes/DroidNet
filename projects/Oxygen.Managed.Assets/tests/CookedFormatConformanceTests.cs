// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text.RegularExpressions;
using AwesomeAssertions;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Assets.Persistence.Pak;

namespace Oxygen.Managed.Assets.Tests;

/// <summary>
/// The engine's version table is the authority on its cooked formats, and a build reads only the current versions. A
/// format the engine moves on from without its managed reader fails here, rather than when the editor opens content.
/// </summary>
[TestClass]
public sealed partial class CookedFormatConformanceTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    [DataRow("kPakVersion", PakIndex.Version)]
    [DataRow("kBrowseIndexVersion", (int)PakIndex.BrowseIndexVersion)]
    [DataRow("kLooseCookedIndexVersion", LooseCookedIndex.Version)]
    [DataRow("kTextureResourceDescriptorVersion", TextureDescriptorFile.Version)]
    public void AManagedReaderReadsTheFormatTheEngineWrites(string constant, int version)
    {
        var table = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Installed", "PakFormatVersions.inc"));
        var row = FormatVersionRow().Matches(table).Single(match => match.Groups["name"].Value == constant);

        _ = int.Parse(row.Groups["version"].Value, CultureInfo.InvariantCulture).Should().Be(version);
    }

    [TestMethod]
    public async Task TheInstalledEnginePakReads()
    {
        var pak = Oxygen.Managed.Core.FileSystemPath.From(Path.Combine(AppContext.BaseDirectory, "Installed", "engine.pak"));

        var document = await PakIndex.ReadFileAsync(pak, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = document.Assets.Should().NotBeEmpty();
        _ = document.CatalogDigest.Should().HaveLength(64);
    }

    [GeneratedRegex(@"OXDAT_FORMAT_VERSION\(\s*(?<name>\w+),\s*\w+,\s*\w+,\s*(?<version>\d+)\)")]
    private static partial Regex FormatVersionRow();
}
