// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class PublisherTerminationTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>A terminated publisher leaves recoverable old output, or a verifiable committed generation.</summary>
    /// <param name="hadPrevious">Whether the project already had published roots.</param>
    /// <param name="boundary">The point where the owned publisher is terminated.</param>
    /// <returns>The asynchronous process interruption regression.</returns>
    [TestMethod]
    [DataRow(true, "Prepared")]
    [DataRow(true, "Applying")]
    [DataRow(true, "SourcesApplied")]
    [DataRow(true, "HeadSelected")]
    [DataRow(true, "RuntimeReady")]
    [DataRow(true, "Committed")]
    [DataRow(false, "Prepared")]
    [DataRow(false, "Applying")]
    [DataRow(false, "SourcesApplied")]
    [DataRow(false, "HeadSelected")]
    [DataRow(false, "RuntimeReady")]
    [DataRow(false, "Committed")]
    public async Task PublisherTerminationRecoversActualFilesystemState(bool hadPrevious, string boundary)
    {
        using var project = new PublicationProject(hadPrevious);
        var start = new ProcessStartInfo(Path.Combine(AppContext.BaseDirectory, "WorkerProbe", "Oxygen.Editor.ContentPipeline.WorkerProbe.exe"))
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        foreach (var argument in new[] { "publish", project.Root, project.Context.ProjectId.ToString("D"), project.Operation.OperationId.ToString("D"), boundary })
        {
            start.ArgumentList.Add(argument);
        }

        using var child = Process.Start(start)!;
        var errors = child.StandardError.ReadToEndAsync(this.TestContext.CancellationToken);
        try
        {
            await WaitForPublicationBoundaryAsync(child, boundary, this.TestContext.CancellationToken).ConfigureAwait(false);
            child.Kill();
            await child.WaitForExitAsync(this.TestContext.CancellationToken).WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
            using var writer = await AcquireAfterProcessExitAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
            var recovered = await CookPublicationTransaction.LoadAsync(project.Context, project.Operation.OperationId, project.Files, project.Manager, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
            if (string.Equals(boundary, "Committed", StringComparison.Ordinal))
            {
                using var selected = await CookPublicationReadLease.OpenUnderGateAsync(project.Context, project.Files, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
                _ = selected.PublicationId.Should().Be(project.Operation.OperationId);
                project.AssertNew();
            }
            else
            {
                await recovered.RecoverAsync(writer).ConfigureAwait(false);
                project.AssertOld();
            }

        }
        finally
        {
            if (!child.HasExited)
            {
                child.Kill();
                await child.WaitForExitAsync(CancellationToken.None).ConfigureAwait(false);
            }

            this.TestContext.WriteLine(await errors.ConfigureAwait(false));
        }
    }

    private static async Task WaitForPublicationBoundaryAsync(Process child, string boundary, CancellationToken cancellationToken)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(10));
        while (await child.StandardOutput.ReadLineAsync(timeout.Token).ConfigureAwait(false) is { } line)
        {
            if (string.Equals(line, "PUBLICATION_BOUNDARY:" + boundary, StringComparison.Ordinal))
            {
                return;
            }
        }

        throw new IOException("The owned publisher exited before reaching the requested boundary.");
    }

    private static async Task<CookOutputWriteLease> AcquireAfterProcessExitAsync(string root, CancellationToken cancellationToken)
    {
        var elapsed = Stopwatch.StartNew();
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(5));
        while (true)
        {
            try
            {
                return await CookOutputLease.AcquireWriteAsync(root, timeout.Token).ConfigureAwait(false);
            }
            catch (CookOutputBusyException) when (elapsed.Elapsed < TimeSpan.FromSeconds(5))
            {
                await Task.Delay(20, timeout.Token).ConfigureAwait(false);
            }
        }
    }
}
