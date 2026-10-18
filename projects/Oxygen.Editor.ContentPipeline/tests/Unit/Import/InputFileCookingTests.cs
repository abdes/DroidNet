// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

/// <summary>
/// Hand-written input files cook as self-contained <c>input</c> jobs whose outputs land next to the file.
/// </summary>
[TestClass]
public sealed class InputFileCookingTests : IDisposable
{
    private const string SelfContained = """
        {
          "actions": [ { "name": "Jump", "type": "bool" }, { "name": "Dash", "type": "bool" } ],
          "contexts": [ { "name": "Gameplay", "mappings": [
            { "action": "Jump", "slot": "Space", "trigger": "pressed" },
            { "action": "Dash", "slot": "LeftShift", "trigger": { "type": "chord", "chord_action": "Jump" } }
          ] } ]
        }
        """;

    private const string UsesForeignActions = """
        {
          "actions": [ { "name": "Jump", "type": "bool" } ],
          "contexts": [ { "name": "Vehicle", "mappings": [
            { "action": "Boost", "slot": "W" },
            { "action": "Jump", "slot": "Space", "triggers": [ { "type": "combo", "combo_actions": [ { "action": "Brake" } ] } ] }
          ] } ]
        }
        """;

    private readonly string root = Path.Combine(Path.GetTempPath(), "oxygen-input-cooking-tests", Guid.NewGuid().ToString("N"));

    public InputFileCookingTests() => Directory.CreateDirectory(Path.Combine(this.root, "Content", "Gameplay"));

    public void Dispose()
    {
        if (Directory.Exists(this.root))
        {
            Directory.Delete(this.root, recursive: true);
        }
    }

    [TestMethod]
    public void FindUndeclaredActionsAcceptsASelfContainedFile()
        => _ = InputFileReferences.FindUndeclaredActions(SelfContained).Should().BeEmpty();

    [TestMethod]
    public void FindUndeclaredActionsNamesMappingAndComboReferences()
        => _ = InputFileReferences.FindUndeclaredActions(UsesForeignActions).Should().Equal("Boost", "Brake");

    [TestMethod]
    public void ResolveInputFileKeepsItsIdentityAndFolder()
    {
        var input = CookInputResolver.Resolve(this.CreateProject(), new Uri("asset:///Content/Gameplay/Player.input.json"), ContentCookInputRole.Primary);

        _ = input.Kind.Should().Be(ContentCookAssetKind.Input);
        _ = input.SourceRelativePath.Should().Be("Content/Gameplay/Player.input.json");
        _ = input.OutputVirtualPath.Should().Be("/Content/Gameplay/Player.input.json");
    }

    [TestMethod]
    public void BuildJobPlacesInputOutputsInTheSourceFolder()
    {
        var input = CookInputResolver.Resolve(this.CreateProject(), new Uri("asset:///Content/Gameplay/Player.input.json"), ContentCookInputRole.Primary);

        var job = new ContentImportManifestBuilder().BuildJob(input, []);

        _ = job.Type.Should().Be("input");
        _ = job.Id.Should().Be("input:Content/Gameplay/Player.input.json");
        _ = job.Name.Should().BeNull("input jobs accept no name");
        _ = job.Layout.Should().Be(new ContentImportLayout("/Content") { DescriptorsDirectory = string.Empty, InputDirectory = "Gameplay" });
    }

    [TestMethod]
    public void RejectInputFilesWithForeignActionsDropsOnlyThatFilesJob()
    {
        var project = this.CreateProject();
        var good = this.WriteInput(project, "Player.input.json", SelfContained);
        var bad = this.WriteInput(project, "Vehicle.input.json", UsesForeignActions);
        var builder = new ContentImportManifestBuilder();
        var manifest = new ContentImportManifest(1, Path.Combine(this.root, ".cooked"), new ContentImportLayout("/Content"), [builder.BuildJob(good, []), builder.BuildJob(bad, [])]);
        var scope = new ContentCookScope(project, new ProjectCookScope(project.ProjectId, this.root, Path.Combine(this.root, ".cooked")), [good, bad], CookTargetKind.Folder);

        var (kept, diagnostics) = ContentPipelineService.RejectInputFilesWithForeignActions(Guid.NewGuid(), scope, manifest);

        _ = kept.Jobs.Select(static job => job.Source).Should().Equal("Content/Gameplay/Player.input.json");
        var diagnostic = diagnostics.Should().ContainSingle().Subject;
        _ = diagnostic.Code.Should().Be(ScriptDiagnosticCodes.InputReferenceUnsupported);
        _ = diagnostic.Message.Should().Contain("'Boost'").And.Contain("'Brake'");
        _ = diagnostic.AffectedPath.Should().Be(bad.SourceAbsolutePath);
    }

    private ContentCookInput WriteInput(ProjectContext project, string name, string text)
    {
        File.WriteAllText(Path.Combine(this.root, "Content", "Gameplay", name), text);
        return CookInputResolver.Resolve(project, new Uri("asset:///Content/Gameplay/" + name), ContentCookInputRole.Primary);
    }

    private ProjectContext CreateProject()
        => ProjectContext.FromProject(new Project(new ProjectInfo("Test", Category.Games, this.root)
        {
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
        }));
}
