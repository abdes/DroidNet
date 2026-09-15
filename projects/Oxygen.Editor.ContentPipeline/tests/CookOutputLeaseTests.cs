// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Exercises real filesystem ownership and the writer-to-runtime handoff.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed partial class CookOutputLeaseTests
{
    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A live process excludes publication; its OS handle release makes its leftover marker reclaimable.</summary>
    /// <param name="terminate">Whether the owned child exits abruptly.</param>
    /// <returns>The asynchronous ownership regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task ExternalReaderMustExitBeforePublication(bool terminate)
    {
        using var project = new ProjectDirectory();
        (await CookOutputLease.AcquireReadAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false)).Dispose();
        var marker = Path.Combine(project.Root, ".build", "cook", "readers", Guid.NewGuid().ToString("N") + ".lease");
        var start = new ProcessStartInfo(Path.Combine(AppContext.BaseDirectory, "WorkerProbe", "Oxygen.Editor.ContentPipeline.WorkerProbe.exe"))
        {
            UseShellExecute = false, CreateNoWindow = true, RedirectStandardInput = true, RedirectStandardOutput = true,
        };
        start.ArgumentList.Add("hold-file");
        start.ArgumentList.Add(marker);
        using var child = Process.Start(start)!;
        try
        {
            _ = (await child.StandardOutput.ReadLineAsync(this.TestContext.CancellationToken).AsTask().WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("ready");
            Action publish = () => CookOutputLease.AcquireWrite(project.Root).Dispose();
            _ = publish.Should().Throw<CookOutputBusyException>();
            if (terminate)
            {
                child.Kill();
            }
            else
            {
                await child.StandardInput.WriteLineAsync("close").ConfigureAwait(false);
            }

            await child.WaitForExitAsync(this.TestContext.CancellationToken).WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
            using var writer = await WaitForReleasedWriterAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = File.Exists(marker).Should().BeFalse();
        }
        finally
        {
            if (!child.HasExited)
            {
                child.Kill();
                await child.WaitForExitAsync(CancellationToken.None).ConfigureAwait(false);
            }
        }
    }

    /// <summary>Multiple readers exclude a writer until every reader has closed.</summary>
    [TestMethod]
    public void ReadersExcludePublicationUntilAllHaveDrained()
    {
        using var project = new ProjectDirectory();
        using var first = CookOutputLease.AcquireRead(project.Root);
        using var second = CookOutputLease.AcquireRead(project.Root);
        Action publish = () => CookOutputLease.AcquireWrite(project.Root).Dispose();
        _ = publish.Should().Throw<CookOutputBusyException>();
        first.Dispose();
        _ = publish.Should().Throw<CookOutputBusyException>();
        second.Dispose();
        using var writer = CookOutputLease.AcquireWrite(project.Root);
        _ = writer.ProjectRoot.Should().Be(project.Root);
    }

    /// <summary>A published runtime inherits its read lease before another writer can start.</summary>
    [TestMethod]
    public void WriterHandsOffToReaderWithoutAnUnprotectedGap()
    {
        using var project = new ProjectDirectory();
        using var writer = CookOutputLease.AcquireWrite(project.Root);
        Action read = () => CookOutputLease.AcquireRead(project.Root).Dispose();
        _ = read.Should().Throw<CookOutputBusyException>();
        using var runtime = writer.CreateReader();
        writer.Dispose();
        Action publish = () => CookOutputLease.AcquireWrite(project.Root).Dispose();
        _ = publish.Should().Throw<CookOutputBusyException>();
        using var otherReader = CookOutputLease.AcquireRead(project.Root);
        runtime.Dispose();
        _ = publish.Should().Throw<CookOutputBusyException>();
        otherReader.Dispose();
        publish();
        Action lateHandoff = () => writer.CreateReader().Dispose();
        _ = lateHandoff.Should().Throw<ObjectDisposedException>();
    }

    /// <summary>Markers left by an exited process do not permanently block the next publication.</summary>
    [TestMethod]
    public void ReleasedMarkerIsReclaimedWithoutTouchingOtherFiles()
    {
        using var project = new ProjectDirectory();
        CookOutputLease.AcquireRead(project.Root).Dispose();
        var readers = Path.Combine(project.Root, ".build", "cook", "readers");
        var marker = Path.Combine(readers, Guid.NewGuid().ToString("N") + ".lease");
        File.WriteAllBytes(marker, []);
        var unrelated = Path.Combine(readers, "notes.txt");
        File.WriteAllText(unrelated, "keep");
        using var writer = CookOutputLease.AcquireWrite(project.Root);
        _ = File.Exists(marker).Should().BeFalse();
        _ = File.ReadAllText(unrelated).Should().Be("keep");
    }

    /// <summary>A publication owner excludes both another writer and newly registered readers.</summary>
    [TestMethod]
    public void CompetingPublisherCannotEnter()
    {
        using var project = new ProjectDirectory();
        using var first = CookOutputLease.AcquireWrite(project.Root);
        Action publish = () => CookOutputLease.AcquireWrite(project.Root).Dispose();
        _ = publish.Should().Throw<CookOutputBusyException>();
        first.Dispose();
        publish();
    }

    private static async Task<CookOutputWriteLease> WaitForReleasedWriterAsync(string root, CancellationToken cancellationToken)
    {
        var deadline = Stopwatch.StartNew();
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(5));
        while (true)
        {
            try
            {
                return await CookOutputLease.AcquireWriteAsync(root, timeout.Token).ConfigureAwait(false);
            }
            catch (CookOutputBusyException) when (deadline.Elapsed < TimeSpan.FromSeconds(5))
            {
                // Process termination can precede completion of filesystem handle cleanup.
                await Task.Delay(20, timeout.Token).ConfigureAwait(false);
            }
        }
    }

    private sealed partial class ProjectDirectory : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("oxygen-output-leases-");

        public string Root => this.directory.FullName;

        public void Dispose() => this.directory.Delete(recursive: true);
    }
}
