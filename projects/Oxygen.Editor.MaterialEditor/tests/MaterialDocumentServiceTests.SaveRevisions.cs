// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Assets.Import.Materials;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Exercises snapshot acknowledgement and save ordering through controlled storage writes.</summary>
public sealed partial class MaterialDocumentServiceTests
{
    /// <summary>Checks both material edit entry points while an earlier snapshot is being written.</summary>
    /// <param name="schemaEdit">Whether to use the schema property edit path.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task EditDuringSavePreservesNewerSourceAndSavedRevision(bool schemaEdit)
    {
        using var workspace = new TempWorkspace();
        var captured = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var service = new MaterialDocumentService(new TestResolver(workspace.Root), new RecordingCookService(), new ControlledFileStore(async (path, bytes, token) =>
        {
            _ = captured.TrySetResult();
            await release.Task.WaitAsync(token).ConfigureAwait(false);
        }));
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/Revision.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.EditScalarAsync(document.DocumentId, new MaterialFieldEdit(MaterialFieldKeys.MetallicFactor, 0.25f), this.TestContext.CancellationToken).ConfigureAwait(false);
        var save = service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken);
        await captured.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = schemaEdit
            ? await service.EditPropertiesAsync(document.DocumentId, PropertyEdit.Single(MaterialDescriptors.Metalness, 0.75f), this.TestContext.CancellationToken).ConfigureAwait(false)
            : await service.EditScalarAsync(document.DocumentId, new MaterialFieldEdit(MaterialFieldKeys.MetallicFactor, 0.75f), this.TestContext.CancellationToken).ConfigureAwait(false);
        release.SetResult();
        var result = await save.ConfigureAwait(false);
        _ = result.Succeeded.Should().BeTrue();
        _ = result.HasUnsavedChanges.Should().BeTrue();
        var current = service.GetDocument(document.DocumentId);
        _ = current.Source.PbrMetallicRoughness.MetallicFactor.Should().Be(0.75f);
        _ = current.IsDirty.Should().BeTrue();
        _ = current.SavedRevision.Should().Be(1);
        _ = current.Revision.Should().Be(2);
        var persisted = MaterialSourceReader.Read(await File.ReadAllBytesAsync(document.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = persisted.PbrMetallicRoughness.MetallicFactor.Should().Be(0.25f);
        Func<Task> close = () => service.CloseAsync(document.DocumentId, discard: false, this.TestContext.CancellationToken);
        _ = await close.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
    }

    /// <summary>Verifies queued saves capture the latest revision and cannot overwrite one another out of order.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task OverlappingSavesSerializeAndCaptureLatestRevision()
    {
        using var workspace = new TempWorkspace();
        var firstWrite = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var writes = 0;
        var service = new MaterialDocumentService(new TestResolver(workspace.Root), new RecordingCookService(), new ControlledFileStore(async (path, bytes, token) =>
        {
            if (Interlocked.Increment(ref writes) == 1)
            {
                firstWrite.SetResult();
                await release.Task.WaitAsync(token).ConfigureAwait(false);
            }
        }));
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/Queued.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.EditScalarAsync(document.DocumentId, new MaterialFieldEdit(MaterialFieldKeys.MetallicFactor, 0.25f), this.TestContext.CancellationToken).ConfigureAwait(false);
        var first = service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken);
        await firstWrite.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var second = service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken);
        _ = writes.Should().Be(1);
        _ = await service.EditScalarAsync(document.DocumentId, new MaterialFieldEdit(MaterialFieldKeys.MetallicFactor, 0.75f), this.TestContext.CancellationToken).ConfigureAwait(false);
        release.SetResult();
        var results = await Task.WhenAll(first, second).ConfigureAwait(false);
        _ = results.Should().OnlyContain(result => result.Succeeded);
        _ = writes.Should().Be(2);
        _ = service.GetDocument(document.DocumentId).IsDirty.Should().BeFalse();
        var persisted = MaterialSourceReader.Read(await File.ReadAllBytesAsync(document.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = persisted.PbrMetallicRoughness.MetallicFactor.Should().Be(0.75f);
    }

    /// <summary>Verifies failure cannot advance the saved revision or overwrite edits made during the write.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task FailedSavePreservesNewerRevisionAndPreviouslyPersistedBytes()
    {
        using var workspace = new TempWorkspace();
        var captured = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var service = new MaterialDocumentService(new TestResolver(workspace.Root), new RecordingCookService(), new ControlledFileStore(async (_, _, token) =>
        {
            captured.SetResult();
            await release.Task.WaitAsync(token).ConfigureAwait(false);
            throw new IOException("Injected write failure");
        }));
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/FailedRevision.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var save = service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken);
        await captured.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.EditScalarAsync(document.DocumentId, new MaterialFieldEdit(MaterialFieldKeys.MetallicFactor, 0.75f), this.TestContext.CancellationToken).ConfigureAwait(false);
        release.SetResult();
        _ = (await save.ConfigureAwait(false)).Succeeded.Should().BeFalse();
        var current = service.GetDocument(document.DocumentId);
        _ = current.SavedRevision.Should().Be(0);
        _ = current.IsDirty.Should().BeTrue();
        _ = current.Source.PbrMetallicRoughness.MetallicFactor.Should().Be(0.75f);
        var persisted = MaterialSourceReader.Read(await File.ReadAllBytesAsync(document.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = persisted.PbrMetallicRoughness.MetallicFactor.Should().Be(0);
    }

    private sealed class ControlledFileStore(Func<string, byte[], CancellationToken, Task> beforeSave) : IAtomicFileStore
    {
        private readonly NativeAtomicFileStore inner = CreateFileStore();

        public Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default)
            => this.inner.ReadAsync(path, cancellationToken);

        public async Task<FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> content, FileVersion expected, CancellationToken cancellationToken = default)
        {
            var bytes = content.ToArray();
            if (expected.Exists)
            {
                await beforeSave(path, bytes, cancellationToken).ConfigureAwait(false);
            }

            return await this.inner.WriteAsync(path, bytes, expected, cancellationToken).ConfigureAwait(false);
        }
    }
}
