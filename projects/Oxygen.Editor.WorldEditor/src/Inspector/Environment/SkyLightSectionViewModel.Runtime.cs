// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Explains, from the rendered state, why the sky light casts no image-based lighting.</summary>
public sealed partial class SkyLightSectionViewModel
{
    // The rendered state trails an edit by a few frames: keep reading until the
    // same answer comes back several times, within a bounded window.
    private const int RuntimeReadAttempts = 40;
    private const int RuntimeStableReads = 3;
    private const int RuntimeMinimumReads = 4;
    private static readonly TimeSpan RuntimeReadInterval = TimeSpan.FromMilliseconds(250);

    private CancellationTokenSource? runtimeWatch;

    /// <summary>Gets why the rendered sky light casts no image-based lighting, or <see langword="null"/> when it does.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasRuntimeStatus))]
    public partial string? RuntimeStatus { get; private set; }

    /// <summary>Gets a value indicating whether the rendered sky light needs the author's attention.</summary>
    public bool HasRuntimeStatus => this.RuntimeStatus is not null;

    /// <summary>Describes the rendered sky-light state for the author.</summary>
    /// <param name="state">The observed runtime environment, or <see langword="null"/> when none is shown.</param>
    /// <returns>A sentence naming the cause, or <see langword="null"/> when the sky light works or is off.</returns>
    internal static string? DescribeRuntime(RuntimeEnvironmentState? state)
    {
        if (state is not { SkyLightObserved: true, SkyLightEnabled: true })
        {
            return null;
        }

        return state.SkyLightUnavailableReason switch
        {
            RuntimeSkyLightUnavailableReason.MissingCubemap => "No cubemap is selected, so the sky light casts no light.",
            RuntimeSkyLightUnavailableReason.ResourceResolveFailed => "The cubemap failed to load, so the sky light casts no light.",
            RuntimeSkyLightUnavailableReason.NotTextureCube => "The selected texture is not a cubemap, so the sky light casts no light.",
            RuntimeSkyLightUnavailableReason.UnsupportedFormat => "The cubemap is an LDR texture and cannot light the scene. Re-import it with a float (HDR) output format.",
            RuntimeSkyLightUnavailableReason.ProcessingFailed => "Image-based lighting failed to process; the log names the cause.",
            RuntimeSkyLightUnavailableReason.GpuProductsPending => "Loading the cubemap…",
            _ when state.SkyLightEmptyCapture => "Nothing to capture: enable the atmosphere, a backdrop that lights the scene, or fog visible in sky captures.",
            _ => null,
        };
    }

    /// <summary>Reads the rendered sky-light state until it settles, replacing any earlier watch.</summary>
    /// <param name="observe">Reads the runtime environment, or <see langword="null"/> when no runtime shows the scene.</param>
    internal void WatchRuntime(Func<CancellationToken, Task<RuntimeEnvironmentState?>>? observe)
    {
        this.StopRuntimeWatch();
        if (observe is null)
        {
            this.RuntimeStatus = null;
            return;
        }

        var watch = new CancellationTokenSource();
        this.runtimeWatch = watch;
        _ = this.ReadRuntimeAsync(observe, watch.Token);
    }

    private void StopRuntimeWatch()
    {
        if (this.runtimeWatch is { } watch)
        {
            this.runtimeWatch = null;
            watch.Cancel();
            watch.Dispose();
        }
    }

    private async Task ReadRuntimeAsync(Func<CancellationToken, Task<RuntimeEnvironmentState?>> observe, CancellationToken cancellationToken)
    {
        try
        {
            string? previous = null;
            var stable = 0;
            for (var read = 0; read < RuntimeReadAttempts; ++read)
            {
                var state = await observe(cancellationToken).ConfigureAwait(true);
                cancellationToken.ThrowIfCancellationRequested();
                var status = DescribeRuntime(state);
                this.RuntimeStatus = status;
                stable = read > 0 && string.Equals(status, previous, StringComparison.Ordinal) ? stable + 1 : 0;
                previous = status;
                if (read + 1 >= RuntimeMinimumReads && stable + 1 >= RuntimeStableReads
                    && state?.SkyLightUnavailableReason != RuntimeSkyLightUnavailableReason.GpuProductsPending)
                {
                    return;
                }

                await Task.Delay(RuntimeReadInterval, cancellationToken).ConfigureAwait(true);
            }
        }
        catch (OperationCanceledException)
        {
            // A newer edit or the section's disposal replaced this watch.
        }
    }
}
