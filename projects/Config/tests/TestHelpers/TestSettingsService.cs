// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace DroidNet.Config.Tests.TestHelpers;

/// <summary>
/// Concrete implementation of SettingsService for ITestSettings interface.
/// This service implements both the abstract SettingsService base class and the ITestSettings interface.
/// </summary>
public sealed class TestSettingsService : SettingsService<ITestSettings>, ITestSettings
{
    private string name = "Default";
    private int value = 42;
    private bool isEnabled = true;
    private string? description;

    public TestSettingsService(SettingsManager manager, ILoggerFactory? loggerFactory = null)
        : base(manager, loggerFactory)
    {
    }

    /// <inheritdoc/>
    public override string SectionName => "TestSettings";

    /// <inheritdoc/>
    protected override Type PocoType => typeof(TestSettings);

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

    protected override ITestSettings GetSettingsSnapshot()
    {
        return new TestSettings
        {
            Name = this.Name,
            Value = this.Value,
            IsEnabled = this.IsEnabled,
            Description = this.Description,
        };
    }

    protected override void UpdateProperties(ITestSettings source)
    {
        Console.WriteLine($"[DEBUG UpdateProperties] Called with source type: {source?.GetType().FullName ?? "NULL"}");
        Console.WriteLine($"[DEBUG UpdateProperties] Source.Name: {source?.Name ?? "NULL"}");
        Console.WriteLine($"[DEBUG UpdateProperties] Source.Value: {source?.Value}");
        Console.WriteLine($"[DEBUG UpdateProperties] Current this.Name before update: {this.Name}");

        if (source == null)
        {
            return;
        }

        this.Name = source.Name;
        this.Value = source.Value;
        this.IsEnabled = source.IsEnabled;
        this.Description = source.Description;

        Console.WriteLine($"[DEBUG UpdateProperties] Current this.Name after update: {this.Name}");
    }

    protected override ITestSettings CreateDefaultSettings()
    {
        return new TestSettings
        {
            Name = "Default",
            Value = 42,
            IsEnabled = true,
            Description = null,
        };
    }
}
