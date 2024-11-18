// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace DroidNet.Config.Tests;

[ExcludeFromCodeCoverage]
public sealed partial class TestSettingsService(
    IOptionsMonitor<TestSettings> settingsMonitor,
    IFileSystem fs,
    ILoggerFactory? loggerFactory = null)
    : SettingsService<TestSettings>(settingsMonitor, fs, loggerFactory)
{
    private string fooString = "InitialFoo";
    private int barNumber = 1;

    public string FooString
    {
        get => this.fooString;
        set => this.SetField(ref this.fooString, value);
    }

    public int BarNumber
    {
        get => this.barNumber;
        set => this.SetField(ref this.barNumber, value);
    }

    /// <inheritdoc/>
    protected override void UpdateProperties(TestSettings newSettings)
    {
        this.FooString = newSettings.FooString;
        this.BarNumber = newSettings.BarNumber;
    }

    /// <inheritdoc/>
    protected override string GetConfigFilePath() => "testConfig.json";

    /// <inheritdoc/>
    protected override string GetConfigSectionName() => "TestSettings";

    /// <inheritdoc/>
    protected override TestSettings GetSettingsSnapshot() =>
        new()
        {
            FooString = this.FooString,
            BarNumber = this.BarNumber,
        };
}
