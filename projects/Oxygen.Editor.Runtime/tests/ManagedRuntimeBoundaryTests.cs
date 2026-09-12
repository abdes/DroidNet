// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline;

namespace Oxygen.Editor.Runtime.Tests;

/// <summary>Proves startup-facing runtime operations are usable when Interop cannot be loaded.</summary>
[TestClass]
public sealed class ManagedRuntimeBoundaryTests
{
    /// <summary>Gets or sets the current test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Copies the real probe without Interop and executes it in an owned isolated process.</summary>
    /// <returns>The asynchronous process test.</returns>
    [TestMethod]
    public async Task RuntimeSettingsAndServiceRemainUsableWithoutInterop()
    {
        var source = Path.Combine(AppContext.BaseDirectory, "ManagedBoundaryProbe");
        var root = Path.Combine(Path.GetTempPath(), "OxygenManagedBoundaryTests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            foreach (var file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
            {
                if (!string.Equals(Path.GetFileName(file), "DroidNet.Oxygen.Editor.Interop.dll", StringComparison.OrdinalIgnoreCase))
                {
                    var destination = Path.Combine(root, Path.GetRelativePath(source, file));
                    Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                    File.Copy(file, destination);
                }
            }

            var result = await new ContentPipelineProcessRunner().RunAsync(
                new(Path.Combine(root, "Oxygen.Editor.Runtime.ManagedBoundaryProbe.exe"), [], root),
                this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = result.ExitCode.Should().Be(0, result.StandardError + Environment.NewLine + result.StandardOutput);
            _ = result.StandardOutput.Should().Contain("Managed runtime ready; Interop not loaded.");
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }
}
