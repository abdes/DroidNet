// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Disposables;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Scripting.Tests;

/// <summary>
/// A project's script tooling files: created, refreshed from the SDK, and merged without overwriting user values.
/// </summary>
[TestClass]
public sealed class ProjectScriptToolingTests
{
    private const string Root = @"C:\Tooling";

    private readonly MockFileSystem files = new();
    private readonly List<OperationResult> published = [];

    public ProjectScriptToolingTests()
    {
        _ = this.files.Directory.CreateDirectory(Path.Combine(Root, "Sdk", "schemas"));
        _ = this.files.Directory.CreateDirectory(Path.Combine(Root, "Project"));
        this.files.File.WriteAllText(DefinitionsPath, "declare oxygen: any\n");
        this.files.File.WriteAllText(Path.Combine(Root, "Sdk", "schemas", "oxygen.input.schema.json"), "{}");
        this.files.File.WriteAllText(Path.Combine(Root, "Sdk", "schemas", "oxygen.name.schema.json"), "{}");
    }

    private static string DefinitionsPath => Path.Combine(Root, "Sdk", "oxygen.d.luau");

    private static string ProjectRoot => Path.Combine(Root, "Project");

    [TestMethod]
    public void EnsureCreatesEveryFileOfANewProjectAndLogsIt()
    {
        var changes = this.CreateTooling().Ensure(CreateProject());

        _ = changes.Should().HaveCount(5);
        _ = this.files.File.Exists(Path.Combine(ProjectRoot, "Types", "oxygen.name.schema.json")).Should().BeTrue();
        _ = this.files.File.ReadAllText(Path.Combine(ProjectRoot, ".luaurc")).Should().Contain("nonstrict");
        _ = this.files.File.ReadAllText(Path.Combine(ProjectRoot, "Types", "oxygen.d.luau")).Should().Be("declare oxygen: any\n");
        var settings = JsonNode.Parse(this.files.File.ReadAllText(Path.Combine(ProjectRoot, ".vscode", "settings.json")))!.AsObject();
        _ = settings["luau-lsp.types.definitionFiles"]!.AsArray().Select(static node => node!.GetValue<string>()).Should().Equal("Types/oxygen.d.luau");
        _ = settings["files.associations"]!["*.lua"]!.GetValue<string>().Should().Be("luau");
        _ = this.published.Should().ContainSingle(result => result.Status == OperationStatus.Succeeded);
    }

    [TestMethod]
    public void EnsureOnACurrentProjectChangesNothing()
    {
        var tooling = this.CreateTooling();
        _ = tooling.Ensure(CreateProject());
        this.published.Clear();

        _ = tooling.Ensure(CreateProject()).Should().BeEmpty();
        _ = this.published.Should().BeEmpty();
    }

    [TestMethod]
    public void EnsureRefreshesDefinitionsWhenTheSdkChanges()
    {
        var tooling = this.CreateTooling();
        _ = tooling.Ensure(CreateProject());
        this.files.File.WriteAllText(DefinitionsPath, "declare oxygen: { log: any }\n");

        _ = tooling.Ensure(CreateProject()).Should().Equal("Refreshed Types/oxygen.d.luau from the SDK");
        _ = this.files.File.ReadAllText(Path.Combine(ProjectRoot, "Types", "oxygen.d.luau")).Should().Contain("log");
    }

    [TestMethod]
    public void AddMissingSettingsKeepsUserValues()
    {
        var settings = new JsonObject
        {
            ["luau-lsp.platform.type"] = "roblox",
            ["files.associations"] = new JsonObject { ["*.lua"] = "lua", ["*.txt"] = "plaintext" },
            ["json.schemas"] = new JsonArray(new JsonObject { ["fileMatch"] = new JsonArray("*.input.json"), ["url"] = "./Types/oxygen.input.schema.json" }),
        };

        var added = ProjectScriptTooling.AddMissingSettings(settings);

        _ = settings["luau-lsp.platform.type"]!.GetValue<string>().Should().Be("roblox");
        _ = settings["files.associations"]!["*.lua"]!.GetValue<string>().Should().Be("lua");
        _ = settings["json.schemas"]!.AsArray().Should().ContainSingle();
        _ = added.Should().BeEquivalentTo("luau-lsp.types.definitionFiles", "luau-lsp.sourcemap.enabled");
    }

    [TestMethod]
    public void EnsureLeavesSettingsWithCommentsUnchangedAndSaysWhatToAdd()
    {
        var path = Path.Combine(ProjectRoot, ".vscode", "settings.json");
        _ = this.files.Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        const string text = "{\n  // mine\n  \"editor.tabSize\": 2\n}\n";
        this.files.File.WriteAllText(path, text);

        _ = this.CreateTooling().Ensure(CreateProject());

        _ = this.files.File.ReadAllText(path).Should().Be(text);
        _ = this.published.Should().Contain(result => result.Status == OperationStatus.Failed
            && result.Diagnostics.Any(static diagnostic => diagnostic.Code == ScriptDiagnosticCodes.ToolingUpdateFailed)
            && result.Message.Contains("luau-lsp.types.definitionFiles", StringComparison.Ordinal));
    }

    private static ProjectContext CreateProject()
        => new()
        {
            ProjectId = Guid.Empty,
            Name = "Test",
            Category = Category.Games,
            ProjectRoot = FileSystemPath.From(ProjectRoot),
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            LocalFolderMounts = [],
        };

    private ProjectScriptTooling CreateTooling()
        => new(new ScriptToolingPaths(DefinitionsPath, Path.Combine(Root, "Sdk", "schemas")), new RecordingResults(this.published), this.files);

    // Records what the tooling publishes; nothing observes it.
    private sealed class RecordingResults(List<OperationResult> published) : IOperationResultPublisher
    {
        public void Publish(OperationResult result) => published.Add(result);

        public IDisposable Subscribe(IObserver<OperationResult> observer) => Disposable.Empty;
    }
}
