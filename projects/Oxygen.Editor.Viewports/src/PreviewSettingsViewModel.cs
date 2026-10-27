// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Config;
using Oxygen.Editor.LivePreview.Settings;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Viewports;

/// <summary>
/// The preview settings a document editor shows next to its viewports: the project's preview preferences and the
/// engine's log verbosity.
/// </summary>
/// <param name="previewSettings">The project's preview preferences.</param>
/// <param name="engineSettings">The engine startup settings, whose logging verbosity this edits.</param>
public sealed class PreviewSettingsViewModel(
    PreviewSettingsService previewSettings,
    ISettingsService<IEngineStartupSettings> engineSettings) : ObservableObject
{
    /// <summary>Gets the shared project preview preferences.</summary>
    public PreviewSettingsService PreviewSettings { get; } = previewSettings ?? throw new ArgumentNullException(nameof(previewSettings));

    /// <summary>Gets or sets the native engine logging verbosity, an engine startup setting the running engine takes live.</summary>
    public int EngineLoggingVerbosity
    {
        get => engineSettings.Settings.LoggingVerbosity;
        set
        {
            if (engineSettings.Settings.LoggingVerbosity != value)
            {
                engineSettings.Settings.LoggingVerbosity = value;
                this.OnPropertyChanged();
            }
        }
    }
}
