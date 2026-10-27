// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.Models;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.RetainedModelScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Import;

/// <summary>
/// The content pipeline is the one writer of model import records: a replacement of a record, as repairing a kept change
/// in its model asks, is written when the record still holds what the requester read, and the model is reimported in the
/// same operation; a record that changed since is left as it is.
/// </summary>
[TestClass]
internal sealed class ModelRecordReplacementTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task AReplacementOfTheRecordRead_IsWrittenAndReimported()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var service = NativeCook.Create(workspace).Service;
        _ = (await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        var record = ModelRecordLookup.TryFind(workspace.ProjectContext, source)!;
        var before = await File.ReadAllBytesAsync(record.RecordPath, this.TestContext.CancellationToken).ConfigureAwait(false);
        var replacement = ModelImportSettings.Parse(before).WithContinuity("Body/Door_Front_L", "Body/Door_FL").ToBytes();

        var result = await service.ReplaceModelRecordAsync(source, before, replacement, workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = (result.IsPublished || result.IsUpToDate).Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Message)));
        var written = ModelImportSettings.Parse(await File.ReadAllBytesAsync(record.RecordPath, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = written.NodeContinuity.Should().Equal([new ModelNodeContinuity("Body/Door_Front_L", "Body/Door_FL")]);
    }

    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task AReplacementOfARecordChangedSince_LeavesItAsItIs()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var service = NativeCook.Create(workspace).Service;
        _ = (await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        var record = ModelRecordLookup.TryFind(workspace.ProjectContext, source)!;
        var current = await File.ReadAllBytesAsync(record.RecordPath, this.TestContext.CancellationToken).ConfigureAwait(false);
        var read = ModelImportSettings.Parse(current).WithContinuity("Body/Mirror", "Body/Old_Mirror").ToBytes();
        var replacement = ModelImportSettings.Parse(current).WithContinuity("Body/Door_Front_L", "Body/Door_FL").ToBytes();

        var result = await service.ReplaceModelRecordAsync(source, read, replacement, workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = (await File.ReadAllBytesAsync(record.RecordPath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(current);
    }
}
