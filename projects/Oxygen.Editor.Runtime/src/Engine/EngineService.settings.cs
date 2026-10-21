// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Engine-wide presentation and logging settings applied to the running native session.</summary>
public sealed partial class EngineService
{
    /// <inheritdoc/>
    public void SetVSyncEnabled(bool enabled)
    {
        this.EnsureIsReadyOrRunning().SetVSyncEnabled(enabled);
        this.LogVSyncSet(enabled);
    }

    /// <inheritdoc/>
    public void SetAlwaysRenderPanes(bool alwaysRender)
    {
        this.EnsureIsReadyOrRunning().SetAlwaysRenderPanes(alwaysRender);
        this.LogAlwaysRenderPanesSet(alwaysRender);
    }

    /// <inheritdoc/>
    public void SetGroundGrid(GroundGridSettings settings)
    {
        ArgumentNullException.ThrowIfNull(settings);
        this.EnsureIsReadyOrRunning().SetGroundGrid(settings);
        this.LogGroundGridSet(settings);
    }

    /// <summary>Applies startup settings that the running engine also takes live, from now on.</summary>
    private void WatchEngineSettings()
    {
        if (this.watchingEngineSettings || this.engineSettingsService is null)
        {
            return;
        }

        this.engineSettingsService.PropertyChanged += this.OnEngineSettingsChanged;
        this.watchingEngineSettings = true;
    }

    private void UnwatchEngineSettings()
    {
        if (!this.watchingEngineSettings || this.engineSettingsService is null)
        {
            return;
        }

        this.engineSettingsService.PropertyChanged -= this.OnEngineSettingsChanged;
        this.watchingEngineSettings = false;
    }

    private void OnEngineSettingsChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (!string.Equals(e.PropertyName, nameof(IEngineStartupSettings.LoggingVerbosity), StringComparison.Ordinal) || this.State is not (EngineServiceState.Ready or EngineServiceState.Running))
        {
            return;
        }

        // A setting changes from the UI: an engine that stopped meanwhile, or refused the value, keeps the saved value
        // for its next start, and the change does not fail the settings binding.
        var verbosity = this.engineSettings.LoggingVerbosity;
        try
        {
            this.EngineLoggingVerbosity = verbosity;
        }
        catch (Exception exception) when (exception is InvalidOperationException || Diagnostics.EngineInteropExceptionPolicy.IsRecoverable(exception))
        {
            this.LogLiveVerbosityNotApplied(verbosity, exception);
        }
    }
}
