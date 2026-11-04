// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.Projects.Mounts;
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
        fileSystem.File.WriteAllText(Path.Combine(Temp, ProjectMounts.FileName), """{"schema_version":2,"root":true}""");
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

        return fileSystem;
    }

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
        _ = mount.State.ToString().Should().Be(expected["state"]!.GetValue<string>(), because);
        _ = OriginText(mount.Origin).Should().Be(expected["origin"]!.GetValue<string>(), because);
        _ = mount.OriginFile.Should().Be(At(expected["origin_file"]), because);
        _ = mount.Location.Should().Be(At(expected["folder"]), because);
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
