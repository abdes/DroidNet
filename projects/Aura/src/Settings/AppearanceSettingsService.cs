// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Config;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Settings;

/// <summary>
///     Specialized <see cref="ISettingsService{TSettings}" /> for the <see cref="AppearanceSettings" />
///     configuration. Provides intuitive access to the settings through properties with change notifications.
/// </summary>
public sealed partial class AppearanceSettingsService(SettingsManager manager, ILoggerFactory? factory = null)
    : SettingsService<IAppearanceSettings>(manager, factory), IAppearanceSettings
{
    private ElementTheme appThemeMode = ElementTheme.Default;
    private string appThemeBackgroundColor = "#00000000";
    private string appThemeFontFamily = "Segoe UI Variable";

    /// <inheritdoc />
    public ElementTheme AppThemeMode
    {
        get => this.appThemeMode;
        [MemberNotNull(nameof(appThemeMode))]
        set => _ = this.SetField(ref this.appThemeMode, value);
    }

    /// <inheritdoc />
    public string AppThemeBackgroundColor
    {
        get => this.appThemeBackgroundColor;
        set => _ = this.SetField(ref this.appThemeBackgroundColor, value);
    }

    /// <inheritdoc />
    public string AppThemeFontFamily
    {
        get => this.appThemeFontFamily;
        set => _ = this.SetField(ref this.appThemeFontFamily, value);
    }

    /// <inheritdoc/>
    public override string SectionName => AppearanceSettings.ConfigSectionName;

    /// <inheritdoc/>
    public override Type SettingsType => typeof(AppearanceSettings);

    /// <inheritdoc/>
    public override SettingsSectionMetadata SectionMetadata { get; set; } = new()
    {
        SchemaVersion = "20251022",
        Service = typeof(AppearanceSettingsService).FullName,
    };

    /// <inheritdoc/>
    protected override IAppearanceSettings CreateDefaultSettings() => new AppearanceSettings();

    /// <inheritdoc/>
    protected override object GetSettingsSnapshot()
        => new AppearanceSettings
        {
            AppThemeMode = this.appThemeMode,
            AppThemeBackgroundColor = this.appThemeBackgroundColor,
            AppThemeFontFamily = this.appThemeFontFamily,
        };

    /// <inheritdoc/>
    protected override void UpdateProperties(IAppearanceSettings newSettings)
    {
        this.AppThemeMode = newSettings.AppThemeMode;
        this.AppThemeBackgroundColor = newSettings.AppThemeBackgroundColor;
        this.AppThemeFontFamily = newSettings.AppThemeFontFamily;
    }
}
