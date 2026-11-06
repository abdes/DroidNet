// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Cooking;

/// <summary>
/// A folder outside the project, mounted as a source mount and bound by the project's own <c>.mounts.json</c>, cooks
/// like <c>Content</c> with the real ImportTool: into the project's own cooked root, recording each source by its
/// mount path, and a change to a source makes it stale until it cooks again.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
internal sealed partial class SharedMountCookTests
{
    private static readonly AssetPath Brick = AssetPath.Parse("/Studio/Materials/Brick.omat");

    public TestContext TestContext { get; set; } = null!;

    /// <summary>A material in a mount outside the project cooks, is current, goes stale on change and cooks again.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AMaterialInAMountOutsideTheProject_CooksAndFollowsItsChanges()
    {
        using var workspace = new SharedMountWorkspace();
        var source = Path.Combine(workspace.Studio, "Materials", "Brick.omat.json");
        await WriteMaterialAsync(source, CreateMaterial("Brick", roughness: 0.7f)).ConfigureAwait(false);
        var service = new MaterialCookService(workspace.NativePipeline.Pipeline, workspace.Session, NullLogger<MaterialCookService>.Instance);
        var request = new MaterialCookRequest(Brick, FileSystemPath.From(workspace.Root), FileSystemPath.From(source));

        var first = await service.CookMaterialAsync(request, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = first.State.Should().Be(MaterialCookState.Cooked, string.Join(Environment.NewLine, first.Cook?.Diagnostics.Select(static issue => $"{issue.Code}: {issue.Message} {issue.TechnicalMessage}") ?? []));
        _ = first.Cook!.InputSnapshot!.Inputs.Should().Contain(input => input.CookPath == "/Studio/Materials/Brick.omat.json");
        var root = workspace.CookedRoot("Studio");
        _ = root.Should().StartWith(Path.Combine(workspace.Root, ".cooked"), "a shared mount cooks into the project's own output");
        var index = File.OpenRead(Path.Combine(root, LooseCookedIndex.FileName));
        await using (index.ConfigureAwait(false))
        {
            var asset = LooseCookedIndex.Read(index).Assets.Should().ContainSingle(static asset => asset.VirtualPath == "/Studio/Materials/Brick.omat").Subject;
            _ = asset.SourcePath.Should().Be("/Studio/Materials/Brick.omat.json", "the recorded source is the mount path, wherever the folder is");
        }

        _ = (await this.ReadStatusAsync(workspace).ConfigureAwait(false)).Freshness.Should().Be(AssetCookFreshness.Current);

        await WriteMaterialAsync(source, CreateMaterial("Brick", roughness: 0.2f)).ConfigureAwait(false);
        _ = (await this.ReadStatusAsync(workspace).ConfigureAwait(false)).Freshness.Should().NotBe(AssetCookFreshness.Current);

        var second = await service.CookMaterialAsync(request, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = second.State.Should().Be(MaterialCookState.Cooked);
        _ = (await this.ReadStatusAsync(workspace).ConfigureAwait(false)).Freshness.Should().Be(AssetCookFreshness.Current);
    }

    /// <summary>A material whose mount this machine has not bound fails its cook with the mount's code.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AMaterialInAnUnboundMount_FailsWithTheMountsCode()
    {
        using var workspace = new SharedMountWorkspace();
        File.Delete(Path.Combine(workspace.Root, ProjectMounts.FileName));
        workspace.Session.ChangeContext(TestProject.Open(TestProject.InfoOf(workspace.Session.Context), new RealFileSystem()).Context);

        var result = await workspace.NativePipeline.Pipeline.CookAssetAsync(Brick, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().Contain(static issue => issue.Code == MountCodes.Unbound);
    }

    private static async Task WriteMaterialAsync(string path, MaterialSource material)
    {
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var stream = File.Create(path);
        await using (stream.ConfigureAwait(false))
        {
            MaterialSourceWriter.Write(stream, material, Brick);
        }
    }

    private static MaterialSource CreateMaterial(string name, float roughness)
        => new(
            name: name,
            pbrMetallicRoughness: new MaterialPbrMetallicRoughness(baseColorR: 0.6f, baseColorG: 0.3f, baseColorB: 0.2f, baseColorA: 1.0f, metallicFactor: 0.0f, roughnessFactor: roughness),
            alphaMode: MaterialAlphaMode.Opaque,
            alphaCutoff: 0.5f,
            doubleSided: false);

    private async Task<AssetCookStatus> ReadStatusAsync(SharedMountWorkspace workspace)
        => (await workspace.NativePipeline.Pipeline.ReadAsync(workspace.Session.Context, [Brick], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();

    // The project, and beside it the studio's shared folder, which the project mounts as Studio on this machine.
    private sealed partial class SharedMountWorkspace : IDisposable
    {
        private readonly string folder = Path.Combine(Path.GetTempPath(), "oxygen-shared-mount-tests", Guid.NewGuid().ToString("N"));
        private Oxygen.Editor.ContentPipeline.Testing.NativeContentPipelineFixture? nativePipeline;

        public SharedMountWorkspace()
        {
            this.Root = Path.Combine(this.folder, "Project");
            this.Studio = Path.Combine(this.folder, "Studio", "Shared");
            _ = Directory.CreateDirectory(this.Studio);
            _ = Directory.CreateDirectory(this.Root);
            File.WriteAllText(Path.Combine(this.Root, ProjectMounts.FileName), $$$"""{"schema_version":{{{ProjectMounts.SchemaVersion}}},"mounts":{"Studio":"{{{this.Studio.Replace('\\', '/')}}}"}}""");
            var project = TestProject.Create(new RealFileSystem(), this.Root, [new ProjectMount(TestProject.ContentMount, MountKind.Source, "Content"), new ProjectMount("Studio")]).Context;
            _ = TestProject.SaveProjectFile(project);
            this.Session = new TestSession(project);
            this.CookCoordinator = new ContentCookCoordinator(this.Session, NullLogger<ContentCookCoordinator>.Instance);
        }

        public string Root { get; }

        public string Studio { get; }

        public TestSession Session { get; }

        public ContentCookCoordinator CookCoordinator { get; }

        public Oxygen.Editor.ContentPipeline.Testing.NativeContentPipelineFixture NativePipeline => this.nativePipeline ??= new(this.Session, this.CookCoordinator);

        public string CookedRoot(string mount)
        {
            var head = System.Text.Json.JsonSerializer.Deserialize<CookPublicationHead>(File.ReadAllBytes(CookPublicationPaths.Head(this.Root)), CookPublicationDocument.JsonOptions)!;
            var document = System.Text.Json.JsonSerializer.Deserialize<CookPublicationDocument>(File.ReadAllBytes(CookPublicationPaths.Document(this.Root, head.PublicationId)), CookPublicationDocument.JsonOptions)!;
            return document.Roots.Single(root => root.Owner == CookPublicationRootOwner.Project && string.Equals(root.Name, mount, StringComparison.Ordinal)).ResolvePath(this.Root);
        }

        public void Dispose()
        {
            this.Session.End();
            this.CookCoordinator.Dispose();
            this.Session.Dispose();
            if (Directory.Exists(this.folder))
            {
                Directory.Delete(this.folder, recursive: true);
            }
        }
    }
}
