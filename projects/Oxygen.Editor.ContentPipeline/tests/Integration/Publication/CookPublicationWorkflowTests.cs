// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.IncrementalCookScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class CookPublicationWorkflowTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A native recook followed by mount failure restores every published byte and its selected head and products.</summary>
    /// <returns>The asynchronous native publication regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task NativeCookWithFailedPreviewRestoresPublishedGeneration()
    {
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var publication = workspace.Publication;
        var pipeline = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility, publication);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        var previous = ReadOutputIdentities(workspace.Root);
        var provenancePath = CookPublicationPaths.Head(workspace.Root);
        var priorProvenance = await File.ReadAllBytesAsync(provenancePath, this.TestContext.CancellationToken).ConfigureAwait(false);
        workspace.WriteText("Content/Materials/Blue.omat.json", workspace.ReadText("Content/Materials/Blue.omat.json").Replace("0.5", "0.8", StringComparison.Ordinal));
        var preview = new FailingPublicationPreview();
        await using var previewLifetime = preview.ConfigureAwait(false);
        using var registration = publication.RegisterPreview(workspace.ProjectContext, () => Task.FromResult<ICookPublicationPreview?>(preview));

        var failed = await pipeline.CookAssetAsync(new Uri("asset:///Content/Materials/Blue.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = failed.Status.Should().Be(OperationStatus.Failed);
        _ = failed.IsPublished.Should().BeFalse();
        _ = failed.Validation!.Succeeded.Should().BeTrue();
        _ = failed.Diagnostics.Should().Contain(item => item.Code == "Cook.PublicationFailed");
        var restored = ReadOutputIdentities(workspace.Root);
        _ = restored.ToDictionary(static pair => pair.Key, static pair => pair.Value.hash, StringComparer.Ordinal)
            .Should().BeEquivalentTo(previous.ToDictionary(static pair => pair.Key, static pair => pair.Value.hash, StringComparer.Ordinal));
        var receiptPath = CookPublicationPaths.Head(workspace.Root);
        _ = restored.Where(pair => !string.Equals(pair.Key, receiptPath, StringComparison.Ordinal)).Should()
            .BeEquivalentTo(previous.Where(pair => !string.Equals(pair.Key, receiptPath, StringComparison.Ordinal)));
        _ = (await File.ReadAllBytesAsync(provenancePath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(priorProvenance);
        _ = preview.Mounts.Should().Be(2);
        _ = api.Imported.Should().OnlyContain(execution => execution.Manifest.Output.Contains(Path.Combine(".cooked", "generations"), StringComparison.Ordinal));
        registration.Dispose();
        AssertCookSucceeded(await pipeline.CookAssetAsync(new Uri("asset:///Content/Materials/Blue.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false));
    }

    /// <summary>Recovery skips live operation ownership and restores the same journal after its owner releases it.</summary>
    /// <returns>The asynchronous recovery-orchestration regression.</returns>
    [TestMethod]
    public async Task MaintenanceDistinguishesLiveAndAbandonedBuilds()
    {
        using var workspace = new CookWorkspace();
        var publication = workspace.Publication;
        var operation = new ContentCookOperation(Guid.NewGuid(), workspace.ProjectContext, 1);
        using var owner = CookOutputLease.AcquireOperation(workspace.Root, operation.OperationId);
        using var baseline = await publication.AcquireReadAsync(workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);
        var staging = await CookStagingArea.CreateAsync(operation, baseline, ["Content"], workspace.Files, workspace.Manager, this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var stagingLifetime = staging.ConfigureAwait(false);
        var path = staging.Roots.Single().Path;
        await File.WriteAllTextAsync(Path.Combine(path, "partial.bin"), "unpublished", this.TestContext.CancellationToken).ConfigureAwait(false);
        staging.RetainForPublication();
        _ = (await publication.MaintainAsync(workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = Directory.Exists(path).Should().BeTrue();
        await staging.DisposeAsync().ConfigureAwait(false);
        await owner.DisposeAsync().ConfigureAwait(false);
        _ = (await publication.MaintainAsync(workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = Directory.Exists(path).Should().BeFalse();
        _ = File.Exists(CookPublicationPaths.Head(workspace.Root)).Should().BeFalse();
        _ = Directory.Exists(Path.Combine(workspace.Root, ".build", "cook", operation.OperationId.ToString("N"))).Should().BeFalse();
    }

    /// <summary>A verified startup generation remains protected until the runtime takes its reader lease.</summary>
    /// <returns>The asynchronous startup ownership regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task StartupVerificationKeepsReadOwnershipThroughMountHandoff()
    {
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var publication = workspace.Publication;
        var pipeline = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility, publication);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        using var mounted = await publication.AcquireForMountAsync(workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);
        var path = mounted.FindProjectRoot("Content")!;
        using var blocked = CookedGeneration.TryClaim(path);
        _ = blocked.Should().BeNull();
        using (var gate = await CookOutputLease.AcquireWriteAsync(workspace.Root, this.TestContext.CancellationToken).ConfigureAwait(false))
        {
            _ = gate.ProjectRoot.Should().Be(workspace.Root);
        }

        mounted.Dispose();
        _ = (await publication.MaintainAsync(workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = Directory.Exists(path).Should().BeTrue();
    }

    /// <summary>A missing generation marker forbids reuse, while intact provenance permits an ordinary cook to rebuild it.</summary>
    /// <returns>The asynchronous metadata repair regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CookRepairsMissingGenerationMarker()
    {
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var publication = workspace.Publication;
        var pipeline = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility, publication);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        using var previous = await publication.AcquireReadAsync(workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);
        var previousPath = previous.FindProjectRoot("Content")!;
        File.Delete(Path.Combine(previousPath, CookedGeneration.MarkerFileName));
        var repaired = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(repaired);
        _ = repaired.IsPublished.Should().BeTrue();
        _ = File.Exists(Path.Combine(previousPath, CookedGeneration.MarkerFileName)).Should().BeFalse();
        using var mounted = await publication.AcquireForMountAsync(workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);
    }

    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow("head")]
    [DataRow("missing-document")]
    [DataRow("document-digest")]
    public async Task UntrustedSelectionStopsCookingWithoutGuessingOrReplacingIt(string damage)
    {
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        AssertCookSucceeded(await service.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        var headPath = CookPublicationPaths.Head(workspace.Root);
        var head = System.Text.Json.JsonSerializer.Deserialize<CookPublicationHead>(await File.ReadAllBytesAsync(headPath, this.TestContext.CancellationToken).ConfigureAwait(false), CookPublicationDocument.JsonOptions)!;
        var documentPath = CookPublicationPaths.Document(workspace.Root, head.PublicationId);
        if (string.Equals(damage, "head", StringComparison.Ordinal))
        {
            await File.WriteAllTextAsync(headPath, "invalid head", this.TestContext.CancellationToken).ConfigureAwait(false);
        }
        else if (string.Equals(damage, "missing-document", StringComparison.Ordinal))
        {
            File.Delete(documentPath);
        }
        else
        {
            await File.AppendAllTextAsync(documentPath, " ", this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var before = await File.ReadAllBytesAsync(headPath, this.TestContext.CancellationToken).ConfigureAwait(false);
        var imports = api.Imported.Count;
        var failed = await service.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = failed.Status.Should().Be(OperationStatus.Failed);
        _ = failed.IsPublished.Should().BeFalse();
        _ = api.Imported.Should().HaveCount(imports);
        _ = (await File.ReadAllBytesAsync(headPath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(before);
    }
}
