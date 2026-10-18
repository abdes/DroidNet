// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Scripting;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Scripting;

/// <summary>
/// A project's script tooling files: created, refreshed from the SDK, and merged without overwriting user values.
/// </summary>
[TestClass]
public sealed partial class ProjectScriptToolingTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "oxygen-script-tooling-tests", Guid.NewGuid().ToString("N"));
    private readonly List<OperationResult> published = [];

    public ProjectScriptToolingTests()
    {
        _ = Directory.CreateDirectory(Path.Combine(this.root, "Sdk", "schemas"));
        _ = Directory.CreateDirectory(Path.Combine(this.root, "Project"));
        File.WriteAllText(this.DefinitionsPath, "declare oxygen: any\n");
        File.WriteAllText(Path.Combine(this.root, "Sdk", "schemas", "oxygen.input.schema.json"), "{}");
        File.WriteAllText(Path.Combine(this.root, "Sdk", "schemas", "oxygen.input-action.schema.json"), "{}");
        File.WriteAllText(Path.Combine(this.root, "Sdk", "schemas", "oxygen.name.schema.json"), "{}");
    }

    private string DefinitionsPath => Path.Combine(this.root, "Sdk", "oxygen.d.luau");

    private string ProjectRoot => Path.Combine(this.root, "Project");

    public void Dispose()
    {
        if (Directory.Exists(this.root))
        {
            Directory.Delete(this.root, recursive: true);
        }
    }

    [TestMethod]
    public void EnsureCreatesEveryFileOfANewProjectAndLogsIt()
    {
        var changes = this.CreateTooling().Ensure(this.CreateProject());

        _ = changes.Should().HaveCount(6);
        _ = File.Exists(Path.Combine(this.ProjectRoot, "Types", "oxygen.name.schema.json")).Should().BeTrue();
        _ = File.ReadAllText(Path.Combine(this.ProjectRoot, ".luaurc")).Should().Contain("nonstrict");
        _ = File.ReadAllText(Path.Combine(this.ProjectRoot, "Types", "oxygen.d.luau")).Should().Be("declare oxygen: any\n");
        var settings = JsonNode.Parse(File.ReadAllText(Path.Combine(this.ProjectRoot, ".vscode", "settings.json")))!.AsObject();
        _ = settings["luau-lsp.types.definitionFiles"]!.AsArray().Select(static node => node!.GetValue<string>()).Should().Equal("Types/oxygen.d.luau");
        _ = settings["files.associations"]!["*.lua"]!.GetValue<string>().Should().Be("luau");
        _ = this.published.Should().ContainSingle(result => result.Status == OperationStatus.Succeeded);
    }

    [TestMethod]
    public void EnsureOnACurrentProjectChangesNothing()
    {
        var tooling = this.CreateTooling();
        _ = tooling.Ensure(this.CreateProject());
        this.published.Clear();

        _ = tooling.Ensure(this.CreateProject()).Should().BeEmpty();
        _ = this.published.Should().BeEmpty();
    }

    [TestMethod]
    public void EnsureRefreshesDefinitionsWhenTheSdkChanges()
    {
        var tooling = this.CreateTooling();
        _ = tooling.Ensure(this.CreateProject());
        File.WriteAllText(this.DefinitionsPath, "declare oxygen: { log: any }\n");

        _ = tooling.Ensure(this.CreateProject()).Should().Equal("Refreshed Types/oxygen.d.luau from the SDK");
        _ = File.ReadAllText(Path.Combine(this.ProjectRoot, "Types", "oxygen.d.luau")).Should().Contain("log");
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
        _ = settings["json.schemas"]!.AsArray().Should().HaveCount(2);
        _ = added.Should().BeEquivalentTo("luau-lsp.types.definitionFiles", "luau-lsp.sourcemap.enabled", "json.schemas *.input-action.json");
    }

    [TestMethod]
    public void EnsureLeavesSettingsWithCommentsUnchangedAndSaysWhatToAdd()
    {
        var path = Path.Combine(this.ProjectRoot, ".vscode", "settings.json");
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        const string text = "{\n  // mine\n  \"editor.tabSize\": 2\n}\n";
        File.WriteAllText(path, text);

        _ = this.CreateTooling().Ensure(this.CreateProject());

        _ = File.ReadAllText(path).Should().Be(text);
        _ = this.published.Should().Contain(result => result.Status == OperationStatus.Failed
            && result.Diagnostics.Any(static diagnostic => diagnostic.Code == ScriptDiagnosticCodes.ToolingUpdateFailed)
            && result.Message.Contains("luau-lsp.types.definitionFiles", StringComparison.Ordinal));
    }

    private ProjectScriptTooling CreateTooling()
    {
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(static p => p.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.published.Add);
        return new ProjectScriptTooling(new ScriptToolingPaths(this.DefinitionsPath, Path.Combine(this.root, "Sdk", "schemas")), publisher.Object);
    }

    private ProjectContext CreateProject()
        => new()
        {
            ProjectId = Guid.Empty,
            Name = "Test",
            Category = Category.Games,
            ProjectRoot = this.ProjectRoot,
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            LocalFolderMounts = [],
        };
}
