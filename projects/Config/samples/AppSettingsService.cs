// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;

namespace DroidNet.Config.Example;

[SuppressMessage("Usage", "CA1812:Avoid uninstantiated internal classes", Justification = "Instantiated by DI container at runtime")]
internal sealed partial class AppSettingsService(SettingsManager manager, ILoggerFactory? factory = null) : SettingsService<IAppSettings>(manager, factory), IAppSettings
{
    private string applicationName = "DroidNet Config Sample";
    private string loggingLevel = "Warning";
    private bool enableExperimental;

    public override string SectionName => "AppSettings";

    public override Type SettingsType => typeof(AppSettings);

    public string ApplicationName { get => this.applicationName; set => this.SetField(ref this.applicationName, value); }

    public string LoggingLevel { get => this.loggingLevel; set => this.SetField(ref this.loggingLevel, value); }

    public bool EnableExperimental { get => this.enableExperimental; set => this.SetField(ref this.enableExperimental, value); }

    protected override object GetSettingsSnapshot() => new AppSettings
    {
        ApplicationName = this.ApplicationName,
        LoggingLevel = this.LoggingLevel,
        EnableExperimental = this.EnableExperimental,
    };

    protected override void UpdateProperties(IAppSettings settings)
    {
        this.ApplicationName = settings.ApplicationName;
        this.LoggingLevel = settings.LoggingLevel;
        this.EnableExperimental = settings.EnableExperimental;
    }

    protected override IAppSettings CreateDefaultSettings() => new AppSettings();
}
