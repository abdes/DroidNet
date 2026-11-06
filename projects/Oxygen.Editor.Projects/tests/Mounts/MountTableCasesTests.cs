// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Projects.Tests.Mounts;

/// <summary>
/// Runs the engine's <c>MountTableCases.json</c>, the case table of mount table resolution, so the editor's reader
/// binds, overrides and refuses exactly what the engine's does, with the same codes.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Unit")]
public sealed class MountTableCasesTests
{
    private const string Temp = @"C:\MountCases";
    private const int ScriptDescriptorSize = 78;
    private const int ScriptExternalSourceOffset = 58;

    private static readonly Lazy<JsonArray> Cases = new(static () =>
        JsonNode.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "MountTableCases.json")))!["cases"]!.AsArray());

    [TestMethod]
    public void Tables_FollowTheCaseTable()
    {
        _ = Cases.Value.Should().NotBeEmpty();
        foreach (var testCase in Cases.Value.Select(static node => node!.AsObject()))
        {
            var name = testCase["name"]!.GetValue<string>();
            var fileSystem = Arrange(testCase);
            var project = FileSystemPath.From(Path.Combine(Temp, testCase["project"]?.GetValue<string>() ?? "ws/proj"));
            _ = fileSystem.Directory.CreateDirectory(project.FullPath);

            var table = ProjectMounts.Resolve(fileSystem, project, Declarations(testCase));

            var expected = testCase["mounts"]!.AsArray().Select(static node => node!.AsObject()).ToList();
            _ = table.Mounts.Should().HaveCount(expected.Count, name);
            foreach (var mount in expected)
            {
                ExpectMount(table, mount, name);
            }

            _ = table.Findings.Select(static finding => finding.Code).Distinct(StringComparer.Ordinal)
                .Should().BeEquivalentTo(testCase["codes"]!.AsArray().Select(static code => code!.GetValue<string>()), name);
        }
    }

    private static MockFileSystem Arrange(JsonObject testCase)
    {
        var fileSystem = new MockFileSystem();
        _ = fileSystem.Directory.CreateDirectory(Temp);
        fileSystem.File.WriteAllText(Path.Combine(Temp, ProjectMounts.FileName), $$$"""{"schema_version":{{{ProjectMounts.SchemaVersion}}},"root":true}""");
        foreach (var folder in Strings(testCase["folders"]))
        {
            _ = fileSystem.Directory.CreateDirectory(Path.Combine(Temp, folder));
        }

        foreach (var file in Strings(testCase["files"]))
        {
            var path = Path.Combine(Temp, file);
            _ = fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            fileSystem.File.WriteAllText(path, "x");
        }

        foreach (var (folder, document) in testCase["mounts_files"]?.AsObject() ?? [])
        {
            _ = fileSystem.Directory.CreateDirectory(Path.Combine(Temp, folder));
            fileSystem.File.WriteAllText(Path.Combine(Temp, folder, ProjectMounts.FileName), Expand(document!.ToJsonString()));
        }

        foreach (var recipe in testCase["cooked_sources"]?.AsArray().Select(static node => node!.AsObject()) ?? [])
        {
            AddCookedSource(fileSystem, recipe);
        }

        return fileSystem;
    }

    // Builds the cooked source a recipe names with the fixture writers.
    private static void AddCookedSource(MockFileSystem fileSystem, JsonObject recipe)
    {
        var path = Path.Combine(Temp, recipe["path"]!.GetValue<string>());
        var unsupported = string.Equals(recipe["format"]?.GetValue<string>(), "unsupported", StringComparison.Ordinal);
        var roots = Strings(recipe["roots"]).ToArray();
        var assets = recipe["assets"]?.AsArray().Select(static asset => (Path: asset!["path"]!.GetValue<string>(), Type: Enum.Parse<AssetType>(asset["type"]!.GetValue<string>()))).ToArray() ?? [];
        if (string.Equals(recipe["kind"]!.GetValue<string>(), "pak", StringComparison.Ordinal))
        {
            _ = fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            using var stream = new MemoryStream();
            PakFixture.Write(stream, PakOf(roots[0], assets, recipe["requirements"]?.AsArray()));
            var bytes = stream.ToArray();
            if (unsupported)
            {
                BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(8), PakIndex.Version + 1);
            }

            fileSystem.File.WriteAllBytes(path, bytes);
            return;
        }

        _ = fileSystem.Directory.CreateDirectory(path);
        var entries = new List<AssetEntry>();
        foreach (var root in roots)
        {
            entries.Add(EntryOf($"/{root}/Materials/Red.omat", AssetType.Material, fileSystem, path, new byte[4]));
        }

        if (recipe["external_script"]?.GetValue<bool>() == true)
        {
            var descriptor = new byte[ScriptDescriptorSize];
            BinaryPrimitives.WriteUInt32LittleEndian(descriptor.AsSpan(ScriptExternalSourceOffset), 1);
            entries.Add(EntryOf($"/{roots[0]}/Scripts/Spin.oscript", AssetType.Script, fileSystem, path, descriptor));
        }

        entries.AddRange(assets.Select(asset => EntryOf(asset.Path, asset.Type, fileSystem, path, new byte[4])));
        using (var index = new MemoryStream())
        {
            var requirements = (recipe["requirements"]?.AsArray() ?? []).Select(static node => new AssetRequirement(
                0,
                node!["mount"]!.GetValue<string>(),
                IsTexture: false,
                AssetKey.FromVirtualPath(node["path"]!.GetValue<string>()),
                (byte)Enum.Parse<AssetType>(node["type"]!.GetValue<string>())));
            LooseCookedIndexFixture.Write(index, new Document(1, default, Guid.CreateVersion7(), entries, []) { Requirements = [.. requirements] });
            var bytes = index.ToArray();
            if (unsupported)
            {
                BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(8), LooseCookedIndex.Version + 1);
            }

            fileSystem.File.WriteAllBytes(Path.Combine(path, LooseCookedIndex.FileName), bytes);
        }
    }

    private static AssetEntry EntryOf(string virtualPath, AssetType type, MockFileSystem fileSystem, string root, byte[] descriptor)
    {
        var relative = virtualPath[1..] + ".bin";
        var file = Path.Combine(root, relative);
        _ = fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(file)!);
        fileSystem.File.WriteAllBytes(file, descriptor);
        return new AssetEntry(AssetKey.FromVirtualPath(virtualPath), relative, virtualPath, (byte)type, (ulong)descriptor.Length, SHA256.HashData(Encoding.UTF8.GetBytes(virtualPath)));
    }

    private static PakDocument PakOf(string root, (string Path, AssetType Type)[] assets, JsonArray? requirements)
        => new(
            1,
            Guid.CreateVersion7(),
            new string('0', 64),
            [.. assets.Select(static asset => new PakAssetEntry(AssetKey.FromVirtualPath(asset.Path), (byte)asset.Type, asset.Path, 0, 0))])
        {
            Library = root,
            Dependencies = [.. (requirements ?? []).Select(static node => node!.AsObject())
                .GroupBy(static requirement => requirement["mount"]!.GetValue<string>(), StringComparer.Ordinal)
                .Select(static group => new LibraryDependency(group.Key, [.. group.Select(static requirement => new LibraryRequirement(
                    false,
                    AssetKey.FromVirtualPath(requirement["path"]!.GetValue<string>()),
                    (byte)Enum.Parse<AssetType>(requirement["type"]!.GetValue<string>())))]))],
        };

    private static List<ProjectMount> Declarations(JsonObject testCase)
        => [.. testCase["declarations"]!.AsArray().Select(static node => new ProjectMount(
            node!["name"]!.GetValue<string>(),
            Enum.Parse<MountKind>(node["kind"]!.GetValue<string>()),
            node["path"] is { } path ? Expand(path.GetValue<string>()) : null))];

    private static void ExpectMount(ProjectMountTable table, JsonObject expected, string because)
    {
        var name = expected["name"]!.GetValue<string>();
        var mount = table.Find(name);
        _ = mount.Should().NotBeNull($"{because}: {name}");
        _ = mount!.Kind.ToString().Should().Be(expected["kind"]!.GetValue<string>(), because);
        _ = mount.Scope.ToString().Should().BeEquivalentTo(expected["scope"]!.GetValue<string>(), because);
        _ = mount.State.ToString().Should().Be(expected["state"]!.GetValue<string>(), $"{because}: {string.Join(' ', mount.Findings.Select(static finding => finding.Message))}");
        _ = OriginText(mount.Origin).Should().Be(expected["origin"]!.GetValue<string>(), because);
        _ = mount.OriginFile.Should().Be(At(expected["origin_file"]), because);
        _ = mount.Location.Should().Be(At(expected["folder"]), because);
        if (expected["role"] is { } role)
        {
            _ = mount.Role.ToString().Should().Be(role.GetValue<string>(), because);
        }
    }

    private static string OriginText(MountOrigin origin) => origin switch
    {
        MountOrigin.Committed => "committed",
        MountOrigin.MountsFile => ".mounts.json",
        _ => "none",
    };

    private static FileSystemPath? At(JsonNode? relative)
        => relative is null ? null : FileSystemPath.From(Path.GetFullPath(Path.Combine(Temp, relative.GetValue<string>())));

    private static string Expand(string text) => text.Replace("${temp}", Temp.Replace('\\', '/'), StringComparison.Ordinal);

    private static IEnumerable<string> Strings(JsonNode? array)
        => array?.AsArray().Select(static node => node!.GetValue<string>()) ?? [];
}
