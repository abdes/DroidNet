// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using System.Text;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Testably.Abstractions;

namespace DroidNet.Storage.Tests.Native;

[TestClass]
public sealed class NativeAtomicFileStoreTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    [DataRow(false, 0)]
    [DataRow(false, 1)]
    [DataRow(false, 2)]
    [DataRow(true, 0)]
    [DataRow(true, 1)]
    [DataRow(true, 2)]
    public async Task StageFailure_PreservesExistingOrMissingDestination(bool existing, int failedStage)
    {
        using var workspace = new Workspace();
        var path = workspace.Path("scene.oscene.json");
        var store = new NativeAtomicFileStore(new RealFileSystem(), async (stage, stream, token) =>
        {
            if ((int)stage != failedStage)
            {
                return;
            }

            if (stage == AtomicWriteStage.BeforeWrite)
            {
                await stream!.WriteAsync("{"u8.ToArray(), token).ConfigureAwait(false);
            }

            throw new IOException("Injected storage failure");
        });
        if (existing)
        {
            await File.WriteAllTextAsync(path, "previous complete document", this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var baseline = await store.ReadAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);
        var saving = () => store.WriteAsync(path, Encoding.UTF8.GetBytes("new complete document"), baseline.Version, this.TestContext.CancellationToken);
        _ = await saving.Should().ThrowExactlyAsync<IOException>().ConfigureAwait(false);

        var after = await store.ReadAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = after.Version.Should().Be(baseline.Version);
        _ = after.Content.Should().Equal(baseline.Content);
        _ = Directory.EnumerateFiles(workspace.Root, "*.tmp").Should().BeEmpty();
        _ = Directory.EnumerateFiles(workspace.Root, "*.lock").Should().BeEmpty();
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task SuccessfulWrite_PublishesCompleteBytesAndNewBaseline(bool existing)
    {
        using var workspace = new Workspace();
        var path = workspace.Path("material.omat.json");
        var store = new NativeAtomicFileStore(new RealFileSystem());
        if (existing)
        {
            await File.WriteAllTextAsync(path, "old", this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var original = await store.ReadAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);
        var bytes = Encoding.UTF8.GetBytes("{\"complete\":true}");
        var written = await store.WriteAsync(path, bytes, original.Version, this.TestContext.CancellationToken).ConfigureAwait(false);

        var after = await store.ReadAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = after.Content.Should().Equal(bytes);
        _ = after.Version.Should().Be(written);
        _ = Directory.EnumerateFiles(workspace.Root).Should().ContainSingle();
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task CancellationBeforeReplacement_LeavesPreviousFileAndCleansTemporary(bool existing)
    {
        using var workspace = new Workspace();
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        var path = workspace.Path("scene.oscene.json");
        if (existing)
        {
            await File.WriteAllTextAsync(path, "saved", this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var store = new NativeAtomicFileStore(new RealFileSystem(), (stage, _, _) => stage == AtomicWriteStage.BeforeReplace
            ? cancellation.CancelAsync() : Task.CompletedTask);
        var before = await store.ReadAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);

        var saving = () => store.WriteAsync(path, Encoding.UTF8.GetBytes("cancelled"), before.Version, cancellation.Token);
        _ = await saving.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);

        var after = await store.ReadAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = after.Version.Should().Be(before.Version);
        _ = after.Content.Should().Equal(before.Content);
        _ = Directory.EnumerateFiles(workspace.Root, "*.tmp").Should().BeEmpty();
    }

    [TestMethod]
    public async Task ExternalChangeDuringTemporaryWrite_RejectsReplacement()
    {
        using var workspace = new Workspace();
        var path = workspace.Path("scene.oscene.json");
        await File.WriteAllTextAsync(path, "opened", this.TestContext.CancellationToken).ConfigureAwait(false);
        var store = new NativeAtomicFileStore(new RealFileSystem(), (stage, _, token) => stage == AtomicWriteStage.BeforeReplace
            ? File.WriteAllTextAsync(path, "external edit", token)
            : Task.CompletedTask);
        var before = await store.ReadAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);

        var saving = () => store.WriteAsync(path, Encoding.UTF8.GetBytes("editor edit"), before.Version, this.TestContext.CancellationToken);
        _ = await saving.Should().ThrowExactlyAsync<StorageWriteConflictException>().ConfigureAwait(false);

        _ = (await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("external edit");
        _ = Directory.EnumerateFiles(workspace.Root).Should().ContainSingle();
    }

    [TestMethod]
    public async Task FirstSaveCollision_DoesNotOverwriteTheNewDestination()
    {
        using var workspace = new Workspace();
        var path = workspace.Path("new.omat.json");
        var store = new NativeAtomicFileStore(new RealFileSystem(), (stage, _, token) => stage == AtomicWriteStage.BeforeReplace
            ? File.WriteAllTextAsync(path, "created by another tool", token)
            : Task.CompletedTask);

        var saving = () => store.WriteAsync(path, Encoding.UTF8.GetBytes("editor content"), FileVersion.Missing, this.TestContext.CancellationToken);
        _ = await saving.Should().ThrowExactlyAsync<StorageWriteConflictException>().ConfigureAwait(false);

        _ = (await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("created by another tool");
    }

    [TestMethod]
    public async Task ConcurrentWriter_IsRejectedByDestinationLease()
    {
        using var workspace = new Workspace();
        var path = workspace.Path("scene.oscene.json");
        var arrived = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var first = new NativeAtomicFileStore(new RealFileSystem(), async (stage, _, token) =>
        {
            if (stage == AtomicWriteStage.BeforeReplace)
            {
                arrived.SetResult();
                await release.Task.WaitAsync(token).ConfigureAwait(false);
            }
        });
        var second = new NativeAtomicFileStore(new RealFileSystem());
        var writing = first.WriteAsync(path, Encoding.UTF8.GetBytes("first"), FileVersion.Missing, this.TestContext.CancellationToken);
        await arrived.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        try
        {
            var competing = () => second.WriteAsync(path, Encoding.UTF8.GetBytes("second"), FileVersion.Missing, this.TestContext.CancellationToken);
            _ = await competing.Should().ThrowExactlyAsync<StorageWriteConflictException>().ConfigureAwait(false);
        }
        finally
        {
            release.SetResult();
        }

        _ = await writing.ConfigureAwait(false);
        _ = (await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("first");
    }

    [TestMethod]
    [DataRow(false, 0)]
    [DataRow(false, 1)]
    [DataRow(false, 2)]
    [DataRow(true, 0)]
    [DataRow(true, 1)]
    [DataRow(true, 2)]
    [DataRow(false, 3)]
    [DataRow(true, 3)]
    public async Task ProcessInterruption_PreservesDestinationAndReleasesWriterLease(bool existing, int interruptedStage)
    {
        using var workspace = new Workspace();
        var path = workspace.Path("interrupted.oscene.json");
        if (existing)
        {
            await File.WriteAllTextAsync(path, "previous complete document", this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var store = new NativeAtomicFileStore(new RealFileSystem());
        var before = await store.ReadAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);
        var start = new ProcessStartInfo(System.IO.Path.Combine(AppContext.BaseDirectory, "AtomicWriteProbe", "DroidNet.Storage.AtomicWriteProbe.exe"))
        {
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        start.ArgumentList.Add(path);
        start.ArgumentList.Add(interruptedStage.ToString(CultureInfo.InvariantCulture));
        using var process = Process.Start(start)!;
        try
        {
            await process.WaitForExitAsync(this.TestContext.CancellationToken).WaitAsync(TimeSpan.FromSeconds(15), this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = process.ExitCode.Should().Be(97);
        }
        finally
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
                await process.WaitForExitAsync(CancellationToken.None).ConfigureAwait(false);
            }
        }

        var after = await store.ReadAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);
        if (interruptedStage == 3)
        {
            _ = after.Content.Should().Equal("new complete document"u8.ToArray());
            _ = Directory.EnumerateFiles(workspace.Root, "*.tmp").Should().BeEmpty();
        }
        else
        {
            _ = after.Version.Should().Be(before.Version);
            _ = after.Content.Should().Equal(before.Content);
            _ = Directory.EnumerateFiles(workspace.Root, "*.tmp").Should().ContainSingle();
        }

        _ = Directory.EnumerateFiles(workspace.Root, "*.lock").Should().BeEmpty();
        _ = await store.WriteAsync(path, Encoding.UTF8.GetBytes("successful retry"), after.Version, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("successful retry");
    }

    [TestMethod]
    public async Task ReplacementDeniedByOpenHandle_PreservesOriginalAndCleansTemporary()
    {
        using var workspace = new Workspace();
        var path = workspace.Path("locked.oscene.json");
        await File.WriteAllTextAsync(path, "saved", this.TestContext.CancellationToken).ConfigureAwait(false);
        var store = new NativeAtomicFileStore(new RealFileSystem());
        var before = await store.ReadAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);
        await using (var lease = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read).ConfigureAwait(false))
        {
            var saving = () => store.WriteAsync(path, Encoding.UTF8.GetBytes("blocked replacement"), before.Version, this.TestContext.CancellationToken);
            _ = await saving.Should().ThrowExactlyAsync<StorageWriteConflictException>().ConfigureAwait(false);
        }

        _ = (await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("saved");
        _ = Directory.EnumerateFiles(workspace.Root).Should().ContainSingle();
    }

    private sealed class Workspace : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("DroidNetAtomic-");

        public string Root => this.directory.FullName;

        public string Path(string name) => System.IO.Path.Combine(this.Root, name);

        public void Dispose() => this.directory.Delete(recursive: true);
    }
}
