// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace DroidNet.Config.Tests.Helpers;

/// <summary>
/// Concrete implementation of SettingsService for IAlternativeTestSettings interface.
/// This service implements both the abstract SettingsService base class and the IAlternativeTestSettings interface.
/// </summary>
public sealed class AlternativeTestSettingsService(
    SettingsManager manager,
    ILoggerFactory? loggerFactory = null)
    : SettingsService<IAlternativeTestSettings>(manager, loggerFactory), IAlternativeTestSettings
{
    private string theme = "Light";
    private int fontSize = 12;
    private bool autoSave = true;

    /// <inheritdoc/>
    public override string SectionName => "AlternativeTestSettings";

    public string Theme
    {
        get => this.theme;
        set => this.SetField(ref this.theme, value);
    }

    public int FontSize
    {
        get => this.fontSize;
        set => this.SetField(ref this.fontSize, value);
    }

    public bool AutoSave
    {
        get => this.autoSave;
        set => this.SetField(ref this.autoSave, value);
    }

    protected override Type PocoType => typeof(AlternativeTestSettings);

    protected override IAlternativeTestSettings GetSettingsSnapshot() => new AlternativeTestSettings
    {
        Theme = this.Theme,
        FontSize = this.FontSize,
        AutoSave = this.AutoSave,
    };

    protected override void UpdateProperties(IAlternativeTestSettings source)
    {
        this.Theme = source.Theme;
        this.FontSize = source.FontSize;
        this.AutoSave = source.AutoSave;
    }

    protected override IAlternativeTestSettings CreateDefaultSettings() => new AlternativeTestSettings
    {
        Theme = "Light",
        FontSize = 12,
        AutoSave = true,
    };
}
