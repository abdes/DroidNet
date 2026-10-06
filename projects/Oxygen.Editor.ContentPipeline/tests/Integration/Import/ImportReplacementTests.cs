// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.DependencyScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.IncrementalCookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.RetainedModelScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class ImportReplacementTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>A replacement preserves source/output identities and becomes current only after publication.</summary>
    /// <param name="failure">The initial replacement failure, or success.</param>
    /// <returns>The asynchronous replacement and retained-candidate regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow("success")]
    [DataRow("native")]
    [DataRow("publication")]
    [DataRow("external")]
    [DataRow("external retry")]
    [DataRow("missing")]
    [DataRow("repair")]
    [DataRow("repair dependency conflict")]
    public async Task ReplacementPublishesSourceAndOutputTogether(string failure)
    {
        using var workspace = new CookWorkspace();
        var external = Directory.CreateTempSubdirectory("OxygenReplacement-");
        try
        {
            var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
            var repair = failure.StartsWith("repair", StringComparison.Ordinal);
            if (repair)
            {
                workspace.WriteMaterial("Content/Materials/Unrelated.omat.json", "Unrelated");
            }

            if (failure == "repair dependency conflict")
            {
                workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
                WriteAuthoredGeometry(workspace, withBuffer: true);
                const string retainedPath = "Content/SourceMedia/DCC/Model/model.gltf";
                var retained = System.Text.Json.Nodes.JsonNode.Parse(workspace.ReadText(retainedPath))!;
                var uri = retained["buffers"]![0]!["uri"]!.GetValue<string>();
                var buffer = Convert.FromBase64String(uri[(uri.IndexOf(',', StringComparison.Ordinal) + 1)..]);
                await File.WriteAllBytesAsync(Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Model/retired.bin"), buffer, this.TestContext.CancellationToken).ConfigureAwait(false);
                retained["buffers"]![0]!["uri"] = "retired.bin";
                workspace.WriteText(retainedPath, retained.ToJsonString());
                var settingsPath = Path.Combine(workspace.Root, retainedPath + NativeSceneImportSettings.SidecarSuffix);
                var settings = NativeSceneImportSettings.Parse(await File.ReadAllBytesAsync(settingsPath, this.TestContext.CancellationToken).ConfigureAwait(false)) with
                {
                    Files = ["model.gltf", "retired.bin"],
                    SourceHash = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(await File.ReadAllBytesAsync(Path.Combine(workspace.Root, retainedPath), this.TestContext.CancellationToken).ConfigureAwait(false))),
                };
                await File.WriteAllBytesAsync(settingsPath, settings.ToBytes(), this.TestContext.CancellationToken).ConfigureAwait(false);
                var geometry = System.Text.Json.Nodes.JsonNode.Parse(workspace.ReadText("Content/Geometry/AuthoredCube.ogeo.json"))!;
                geometry["buffers"]![0]!["uri"] = "../SourceMedia/DCC/Model/retired.bin";
                workspace.WriteText("Content/Geometry/AuthoredCube.ogeo.json", geometry.ToJsonString());
            }

            using var compatibility = EditorNativeCompatibilityService.ForCooking();
            var runner = new FailImportBatchRunner { Fail = false };
            var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
            var publication = workspace.Publication;
            var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility, publication);
            var first = repair
                ? await service.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)
                : await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = first.IsPublished.Should().BeTrue();
            if (repair)
            {
                var payload = Directory.EnumerateFiles(Path.Combine(workspace.Root, ".cooked"), "buffers.data", SearchOption.AllDirectories).Single();
                var damaged = await File.ReadAllBytesAsync(payload, this.TestContext.CancellationToken).ConfigureAwait(false);
                damaged[^1] ^= 0xFF;
                await File.WriteAllBytesAsync(payload, damaged, this.TestContext.CancellationToken).ConfigureAwait(false);
            }

            var primary = Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Model/model.gltf");
            var originalBytes = await File.ReadAllBytesAsync(primary, this.TestContext.CancellationToken).ConfigureAwait(false);
            var incoming = Path.Combine(external.FullName, "replacement.gltf");
            var json = System.Text.Json.Nodes.JsonNode.Parse(originalBytes)!;
            json["materials"]![0]!["pbrMetallicRoughness"]!["roughnessFactor"] = 0.1;
            if (failure == "repair dependency conflict")
            {
                var bytes = await File.ReadAllBytesAsync(Path.Combine(Path.GetDirectoryName(primary)!, "retired.bin"), this.TestContext.CancellationToken).ConfigureAwait(false);
                json["buffers"]![0]!["uri"] = "data:application/octet-stream;base64," + Convert.ToBase64String(bytes);
            }

            await this.WriteReplacementCandidateAsync(json, incoming, failure is "external" or "external retry" or "repair dependency conflict").ConfigureAwait(false);
            if (string.Equals(failure, "missing", StringComparison.Ordinal))
            {
                File.Delete(primary);
            }

            var review = await SceneImportReplacement.ReviewAsync(workspace.ProjectContext, "Model", this.TestContext.CancellationToken).ConfigureAwait(false);
            var request = new SceneImportRequest(workspace.ProjectContext, incoming, "Model", new("asset:///Content/Models")) { Replacement = review };
            var preview = new FailingPublicationPreview();
            await using var previewLifetime = preview.ConfigureAwait(false);
            using var registration = string.Equals(failure, "publication", StringComparison.Ordinal) ? publication.RegisterPreview(workspace.ProjectContext, () => Task.FromResult<ICookPublicationPreview?>(preview)) : null;
            runner.Fail = failure is "native" or "external retry";
            var result = await service.ImportSourceAsync(request, this.TestContext.CancellationToken).ConfigureAwait(false);
            if (failure == "repair dependency conflict")
            {
                _ = result.IsPublished.Should().BeFalse();
                _ = result.Diagnostics.Should().Contain(static diagnostic => diagnostic.Code == "asset_cook.replacement_dependency_missing"
                    && diagnostic.AffectedPath != null && diagnostic.AffectedPath.EndsWith("retired.bin", StringComparison.Ordinal));
                _ = (await File.ReadAllBytesAsync(primary, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(originalBytes);
                return;
            }

            if (failure is "native" or "publication" or "external retry")
            {
                _ = result.Status.Should().Be(OperationStatus.Failed);
                _ = (await File.ReadAllBytesAsync(primary, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(originalBytes);
                var retry = workspace.CookCoordinator.Runs.Single(run => run.Request.Import?.Replacement is not null).Request.Import!;
                _ = retry.ReplacementCandidatePath.Should().NotBeNull();
                foreach (var path in Directory.EnumerateFiles(external.FullName, "*", SearchOption.AllDirectories))
                {
                    File.Delete(path);
                }

                runner.Fail = false;
                registration?.Dispose();
                result = await service.ImportSourceAsync(retry, this.TestContext.CancellationToken).ConfigureAwait(false);
            }

            await this.AssertReplacementReadyAsync(workspace, service, source, first, result, primary, json.ToJsonString()).ConfigureAwait(false);
        }
        finally
        {
            external.Delete(recursive: true);
        }
    }

    /// <summary>Conflicting authored state and changed native identities preserve source and published output.</summary>
    /// <param name="conflict">The replacement conflict.</param>
    /// <returns>The asynchronous failed-replacement regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow("source changed")]
    [DataRow("authored output")]
    [DataRow("renamed output")]
    [DataRow("dirty during cook")]
    public async Task ReplacementConflictsPreserveSourceAndOutput(string conflict)
    {
        using var workspace = new CookWorkspace();
        var external = Directory.CreateTempSubdirectory("OxygenReplacementConflict-");
        try
        {
            var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
            using var compatibility = EditorNativeCompatibilityService.ForCooking();
            var runner = new ContextLeaseRunner();
            var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
            var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
            _ = (await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
            var before = ReadOutputIdentities(workspace.Root).ToDictionary(static item => item.Key, static item => item.Value.hash, StringComparer.Ordinal);
            var primary = Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Model/model.gltf");

            var review = await SceneImportReplacement.ReviewAsync(workspace.ProjectContext, "Model", this.TestContext.CancellationToken).ConfigureAwait(false);
            var incoming = Path.Combine(external.FullName, "model.gltf");
            var json = System.Text.Json.Nodes.JsonNode.Parse(await File.ReadAllBytesAsync(primary, this.TestContext.CancellationToken).ConfigureAwait(false))!;
            json["materials"]![0]!["pbrMetallicRoughness"]!["roughnessFactor"] = 0.1;
            if (string.Equals(conflict, "renamed output", StringComparison.Ordinal))
            {
                json["meshes"]![0]!["name"] = "RenamedMesh";
            }

            await File.WriteAllTextAsync(incoming, json.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
            if (string.Equals(conflict, "source changed", StringComparison.Ordinal))
            {
                await File.AppendAllTextAsync(primary, " ", this.TestContext.CancellationToken).ConfigureAwait(false);
            }

            if (string.Equals(conflict, "authored output", StringComparison.Ordinal))
            {
                workspace.WriteMaterial("Content/Materials/Models/Model/Custom.omat.json", "Custom");
            }

            var original = await File.ReadAllBytesAsync(primary, this.TestContext.CancellationToken).ConfigureAwait(false);
            var dirty = false;
            var state = new global::Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentState(Guid.NewGuid(), primary, "Model", 1, 1, IsDirty: true, Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(original)));
            using var registration = workspace.Documents.Register(primary, _ => Task.FromResult<global::Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentReadLease?>(dirty ? new(state, static () =>
{ }) : null));
            runner.BeforeBatch = () =>
            {
                dirty = string.Equals(conflict, "dirty during cook", StringComparison.Ordinal);
                return Task.CompletedTask;
            };
            var result = await service.ImportSourceAsync(new(workspace.ProjectContext, incoming, "Model", new("asset:///Content/Models")) { Replacement = review }, this.TestContext.CancellationToken).ConfigureAwait(false);

            _ = result.Status.Should().Be(OperationStatus.Failed);
            _ = (await File.ReadAllBytesAsync(primary, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(original);
            _ = ReadOutputIdentities(workspace.Root).ToDictionary(static item => item.Key, static item => item.Value.hash, StringComparer.Ordinal).Should().BeEquivalentTo(before);
        }
        finally
        {
            external.Delete(recursive: true);
        }
    }

    /// <summary>A retained source cannot claim unrelated files when the replacement is reviewed.</summary>
    /// <returns>The asynchronous source ownership regression.</returns>
    [TestMethod]
    public async Task ReplacementReviewPreservesUnownedFiles()
    {
        using var workspace = new CookWorkspace();
        _ = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var notes = Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Model/notes.txt");
        await File.WriteAllTextAsync(notes, "unrelated work", this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> review = () => SceneImportReplacement.ReviewAsync(workspace.ProjectContext, "Model", this.TestContext.CancellationToken);
        _ = await review.Should().ThrowAsync<InvalidDataException>().WithMessage("*notes.txt*").ConfigureAwait(false);
        _ = (await File.ReadAllTextAsync(notes, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("unrelated work");
    }

    private async Task AssertReplacementReadyAsync(CookWorkspace workspace, ContentPipelineService service, Uri source, ContentCookResult first, ContentCookResult result, string primary, string expected)
    {
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        _ = result.RetainedSourceUri.Should().Be(source);
        _ = result.CookedAssets.Select(static output => output.CookedAssetUri).Should().BeEquivalentTo(first.CookedAssets.Select(static output => output.CookedAssetUri));
        _ = (await File.ReadAllTextAsync(primary, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(expected);
        _ = (await service.ReadAsync(workspace.ProjectContext, [source], this.TestContext.CancellationToken).ConfigureAwait(false)).Single().Freshness.Should().Be(AssetCookFreshness.Current);
        _ = (await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }

    private async Task WriteReplacementCandidateAsync(System.Text.Json.Nodes.JsonNode json, string incoming, bool externalBuffer)
    {
        if (externalBuffer)
        {
            var uri = json["buffers"]![0]!["uri"]!.GetValue<string>();
            var bytes = Convert.FromBase64String(uri[(uri.IndexOf(',', StringComparison.Ordinal) + 1)..]);
            var directory = Path.Combine(Path.GetDirectoryName(incoming)!, "buffers");
            _ = Directory.CreateDirectory(directory);
            await File.WriteAllBytesAsync(Path.Combine(directory, "new.bin"), bytes, this.TestContext.CancellationToken).ConfigureAwait(false);
            json["buffers"]![0]!["uri"] = "buffers/new.bin";
        }

        await File.WriteAllTextAsync(incoming, json.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
    }
}
