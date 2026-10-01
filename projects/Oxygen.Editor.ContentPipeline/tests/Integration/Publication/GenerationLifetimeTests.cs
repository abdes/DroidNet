// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;
using static Oxygen.Editor.ContentPipeline.TestSupport.GenerationScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

[TestClass]
public sealed class GenerationLifetimeTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task ExternalGenerationHandleDefersReclamationButNotPublication(bool terminate)
    {
        using var project = new ProjectDirectory();
        var root = Path.Combine(project.Root, "generation");
        Directory.CreateDirectory(root);
        var marker = Path.Combine(root, CookedGeneration.MarkerFileName);
        var start = new ProcessStartInfo(Path.Combine(AppContext.BaseDirectory, "WorkerProbe", "Oxygen.Editor.ContentPipeline.WorkerProbe.exe"))
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
        };
        start.ArgumentList.Add("hold-file");
        start.ArgumentList.Add(marker);
        using var child = Process.Start(start)!;
        try
        {
            _ = (await child.StandardOutput.ReadLineAsync(this.TestContext.CancellationToken).AsTask().WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("ready");
            using var refused = CookedGeneration.TryClaim(root);
            _ = refused.Should().BeNull();
            using (var gate = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false))
            {
                _ = gate.ProjectRoot.Should().Be(project.Root);
            }

            if (terminate)
            {
                child.Kill();
            }
            else
            {
                await child.StandardInput.WriteLineAsync("close").ConfigureAwait(false);
            }

            await child.WaitForExitAsync(this.TestContext.CancellationToken).WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
            var release = Stopwatch.StartNew();
            var reclaimed = ReclaimGeneration(root);
            while (!reclaimed && release.Elapsed < TimeSpan.FromSeconds(5))
            {
                await Task.Delay(20, this.TestContext.CancellationToken).ConfigureAwait(false);
                reclaimed = ReclaimGeneration(root);
            }

            _ = reclaimed.Should().BeTrue();
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

    [TestMethod]
    public void RetryInputOwnershipDoesNotPreventOperationRecovery()
    {
        using var project = new ProjectDirectory();
        var id = Guid.NewGuid();
        using var operation = CookOutputLease.AcquireOperation(project.Root, id);
        using var retry = CookOutputLease.TryAcquireRetryInput(project.Root, id);
        _ = retry.Should().NotBeNull();
        using var competing = CookOutputLease.TryAcquireOperation(project.Root, id);
        _ = competing.Should().BeNull();
        operation.Dispose();
        using var recovery = CookOutputLease.AcquireOperation(project.Root, id);
        using var cleanup = CookOutputLease.TryAcquireRetryInput(project.Root, id);
        _ = cleanup.Should().BeNull();
    }
}
