// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;

namespace DroidNet.Config.Tests.Helpers;

/// <summary>
/// Concrete implementation of SettingsService for ITestSettings interface.
/// This service implements both the abstract SettingsService base class and the ITestSettings interface.
/// </summary>
[ExcludeFromCodeCoverage]
public sealed class TestSettingsService(SettingsManager manager, ILoggerFactory? loggerFactory = null)
    : SettingsService<ITestSettings>(manager, loggerFactory), ITestSettings
{
    private string name = "Default";
    private int value = 42;
    private bool isEnabled = true;
    private string? description;

    /// <inheritdoc/>
    public override string SectionName => "TestSettings";

    public string Name
    {
        get => this.name;
        set => this.SetField(ref this.name, value);
    }

    public int Value
    {
        get => this.value;
        set => this.SetField(ref this.value, value);
    }

    public bool IsEnabled
    {
        get => this.isEnabled;
        set => this.SetField(ref this.isEnabled, value);
    }

    public string? Description
    {
        get => this.description;
        set => this.SetField(ref this.description, value);
    }

    public override Type SettingsType => typeof(TestSettings);

    public override SettingsSectionMetadata SectionMetadata { get; set; } = new()
    {
        SchemaVersion = "20251022",
        Service = typeof(TestSettingsService).FullName,
    };

    protected override ITestSettings GetSettingsSnapshot() => new TestSettings
    {
        Name = this.Name,
        Value = this.Value,
        IsEnabled = this.IsEnabled,
        Description = this.Description,
    };

    protected override void UpdateProperties(ITestSettings source)
    {
        if (source is null)
        {
            return;
        }

        this.Name = source.Name;
        this.Value = source.Value;
        this.IsEnabled = source.IsEnabled;
        this.Description = source.Description;
    }

    protected override ITestSettings CreateDefaultSettings() => new TestSettings
    {
        Name = "Default",
        Value = 42,
        IsEnabled = true,
        Description = null,
    };
}
