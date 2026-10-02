// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Managed.Core.Tests;

/// <summary>Checks producer observation independently of native operation ownership.</summary>
public sealed partial class NativeSdkCompatibilityTests
{
    /// <summary>Editor and native producer edits invalidate display identity without a verification request.</summary>
    /// <param name="editor">Whether to change an editor producer instead of an engine tool.</param>
    /// <returns>The asynchronous observation regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task ProducerEditsInvalidateObservation(bool editor)
    {
        using var fixture = new Fixture();
        fixture.CreateCookingInputs();
        using var service = new EditorNativeCompatibilityService(fixture.Installation, cooking: true);
        var first = await service.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = first.Succeeded.Should().BeTrue();
        var fingerprint = first.Artifacts!.Fingerprint;
        await first.Artifacts.DisposeAsync().ConfigureAwait(false);
        _ = service.Observation.Fingerprint.Should().Be(fingerprint);
        var invalidated = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        service.ObservationChanged += (_, _) =>
        {
            if (service.Observation.Fingerprint is null)
            {
                _ = invalidated.TrySetResult();
            }
        };
        var file = editor ? Path.Combine(fixture.Installation.EditorRoot, "Oxygen.Editor.ContentPipeline.dll")
            : Path.Combine(fixture.Installation.EngineRoot, "bin", "Oxygen.Cooker.ImportTool.exe");
        await File.WriteAllTextAsync(file, "changed producer", this.TestContext.CancellationToken).ConfigureAwait(false);
        await invalidated.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        var next = await service.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var lifetime = next.Artifacts!;
        _ = next.Succeeded.Should().BeTrue();
        _ = next.Artifacts!.Fingerprint.Should().NotBe(fingerprint);
    }

    /// <summary>Malformed schemas remain operation diagnostics even when monitoring is initialized.</summary>
    /// <returns>The asynchronous diagnostic regression.</returns>
    [TestMethod]
    public async Task MalformedSchemaDoesNotEscapeVerification()
    {
        using var fixture = new Fixture();
        fixture.CreateCookingInputs();
        await File.WriteAllTextAsync(Path.Combine(fixture.Installation.SchemaDirectory, "oxygen.scene-descriptor.schema.json"),
            "invalid json", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var service = new EditorNativeCompatibilityService(fixture.Installation, cooking: true);
        var result = await service.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().NotBeEmpty();
        _ = service.Observation.Fingerprint.Should().BeNull();
    }

    /// <summary>Display listeners cannot invalidate a successful native ownership transfer.</summary>
    /// <returns>The asynchronous listener-isolation regression.</returns>
    [TestMethod]
    public async Task ListenerFailureDoesNotFailVerification()
    {
        using var fixture = new Fixture();
        fixture.CreateCookingInputs();
        using var service = new EditorNativeCompatibilityService(fixture.Installation, cooking: true);
        var notified = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        service.ObservationChanged += (_, _) => throw new InvalidOperationException("Controlled listener failure.");
        service.ObservationChanged += (_, _) => notified.TrySetResult();
        var result = await service.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var lifetime = result.Artifacts!;
        _ = result.Succeeded.Should().BeTrue();
        await notified.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = service.Observation.Fingerprint.Should().Be(result.Artifacts!.Fingerprint);
    }

    /// <summary>Replacing the SDK directory invalidates observation and the next operation rearms monitoring.</summary>
    /// <param name="wholeInstallation">Whether to replace the installation instead of its binaries directory.</param>
    /// <param name="bundled">Whether the SDK is nested inside the editor installation.</param>
    /// <returns>The asynchronous directory-replacement regression.</returns>
    [TestMethod]
    [DataRow(false, false)]
    [DataRow(true, false)]
    [DataRow(true, true)]
    public async Task ReplacedSdkDirectoryIsMonitoredAgain(bool wholeInstallation, bool bundled)
    {
        using var fixture = new Fixture(bundled);
        fixture.CreateCookingInputs();
        using var service = new EditorNativeCompatibilityService(fixture.Installation, cooking: true);
        var first = await service.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        await first.Artifacts!.DisposeAsync().ConfigureAwait(false);
        var invalidated = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        service.ObservationChanged += (_, _) =>
        {
            if (service.Observation.Fingerprint is null)
            {
                _ = invalidated.TrySetResult();
            }
        };
        var bin = Path.Combine(fixture.Installation.EngineRoot, "bin");
        var replaced = bundled ? fixture.Installation.EditorRoot : wholeInstallation ? fixture.Installation.EngineRoot : bin;
        var retired = replaced + ".retired";
        Directory.Move(replaced, retired);
        await invalidated.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        Directory.Move(retired, replaced);
        var next = await service.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = next.Succeeded.Should().BeTrue();
        await next.Artifacts!.DisposeAsync().ConfigureAwait(false);
        _ = service.Observation.Fingerprint.Should().NotBeNull();
        invalidated = new(TaskCreationOptions.RunContinuationsAsynchronously);
        await File.WriteAllTextAsync(Path.Combine(bin, "Oxygen.Cooker.ImportTool.exe"), "replacement update", this.TestContext.CancellationToken).ConfigureAwait(false);
        await invalidated.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
    }
}
