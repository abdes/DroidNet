// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Inspection;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class ProjectAssetKeyTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Content edits reuse identities; name changes and deletion update the candidate set.</summary>
    /// <returns>The asynchronous cache regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ProjectKeyCacheTracksNamesWithoutRecooking()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Shared.omat.json", "Shared");
        workspace.WriteText("Content/Scripts/Spin.oscript.json", "{}");
        workspace.WriteText("Content/Input/Jump.oiact.json", "{}");
        workspace.WriteText("Content/Input/Gameplay.oimap.json", "{}");
        workspace.WriteText("Content/Physics/Body.opscene.json", "{}");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var index = await ProjectAssetKeyIndex.ReadAsync(workspace.ProjectContext, [], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = index.IsPending.Should().BeTrue();
        _ = runner.Count.Should().Be(0);
        _ = (await index.EnsureAsync(api, workspace.Root, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        var path = Directory.EnumerateFiles(Path.Combine(workspace.Root, ".build/cache/project-asset-keys-v1")).Single();
        var cached = JsonNode.Parse(await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false))!;
        var map = CookedAssetKeyMap.Parse(cached["Report"]!.GetValue<string>());
        var key = map.PathsByKey.Single(static pair => string.Equals(pair.Value, "/Content/Materials/Shared.omat", StringComparison.Ordinal)).Key;
        _ = index.Resolve(key).Should().Be(new Uri("asset:///Content/Materials/Shared.omat"));
        _ = map.PathsByKey.Values.Should().Contain(["/Content/Scripts/Spin.oscript", "/Content/Input/Jump.oiact", "/Content/Input/Gameplay.oimap", "/Content/Physics/Body.opscene"]);
        workspace.WriteMaterial("Content/Materials/Shared.omat.json", "Changed content");
        index = await ProjectAssetKeyIndex.ReadAsync(workspace.ProjectContext, [], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await index.EnsureAsync(api, workspace.Root, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeFalse();
        _ = runner.Count.Should().Be(1);
        File.Move(Path.Combine(workspace.Root, "Content/Materials/Shared.omat.json"), Path.Combine(workspace.Root, "Content/Materials/Étain.omat.json"));
        index = await ProjectAssetKeyIndex.ReadAsync(workspace.ProjectContext, [], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = index.IsPending.Should().BeTrue();
        _ = (await index.EnsureAsync(api, workspace.Root, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = index.Resolve(key).Should().BeNull();
        _ = runner.Count.Should().Be(2);
        File.Delete(Path.Combine(workspace.Root, "Content/Materials/Étain.omat.json"));
        index = await ProjectAssetKeyIndex.ReadAsync(workspace.ProjectContext, [], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = index.IsPending.Should().BeFalse();
        _ = index.Resolve(key).Should().BeNull();
        _ = runner.Count.Should().Be(2);
        _ = File.Exists(global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationPaths.Head(workspace.Root)).Should().BeFalse();
    }

    /// <summary>Malformed and semantically mismatched cached maps cannot prove a candidate's identity.</summary>
    /// <param name="wrongPaths">Whether the report has a valid digest but wrong path set.</param>
    /// <returns>The asynchronous corruption regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow(false)]
    [DataRow(true)]
    public async Task ProjectKeyCacheRejectsCorruptionAndWrongCandidates(bool wrongPaths)
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Shared.omat.json", "Shared");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var index = await ProjectAssetKeyIndex.ReadAsync(workspace.ProjectContext, [], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await index.EnsureAsync(api, workspace.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var path = Directory.EnumerateFiles(Path.Combine(workspace.Root, ".build/cache/project-asset-keys-v1")).Single();
        var replacement = "broken";
        if (wrongPaths)
        {
            var cached = JsonNode.Parse(await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false))!;
            var report = JsonNode.Parse(cached["Report"]!.GetValue<string>())!;
            report["assets"]![0]!["virtual_path"] = "/Content/Materials/Other.omat";
            replacement = report.ToJsonString();
            cached["Digest"] = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(replacement)));
            cached["Report"] = replacement;
            replacement = cached.ToJsonString();
        }

        await File.WriteAllTextAsync(path, replacement, this.TestContext.CancellationToken).ConfigureAwait(false);
        index = await ProjectAssetKeyIndex.ReadAsync(workspace.ProjectContext, [], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = index.IsPending.Should().BeTrue();
        _ = runner.Count.Should().Be(1, "cache reads must not launch native processes");
        _ = (await index.EnsureAsync(api, workspace.Root, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = index.IsPending.Should().BeFalse();
        _ = runner.Count.Should().Be(2);
    }
}
