// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;

namespace DroidNet.Config.Tests.Helpers;

/// <summary>
/// Concrete implementation of SettingsService for ITestSettingsWithValidation interface.
/// This service implements both the abstract SettingsService base class and the ITestSettingsWithValidation interface.
/// Used for testing validation failure scenarios.
/// </summary>
[ExcludeFromCodeCoverage]
public sealed class TestSettingsServiceWithValidation(SettingsManager manager, ILoggerFactory? loggerFactory = null)
    : SettingsService<ITestSettingsWithValidation>(manager, loggerFactory), ITestSettingsWithValidation
{
    private string? requiredField; // Invalid: null/empty
    private int outOfRangeValue = -1; // Invalid: out of range (should be 1-10)
    private string? invalidEmail = "not-an-email"; // Invalid: not an email

    public override string SectionName => "InvalidTestSettings";

    public string? RequiredField
    {
        get => this.requiredField;
        set => this.SetField(ref this.requiredField, value);
    }

    public int OutOfRangeValue
    {
        get => this.outOfRangeValue;
        set => this.SetField(ref this.outOfRangeValue, value);
    }

    public string? InvalidEmail
    {
        get => this.invalidEmail;
        set => this.SetField(ref this.invalidEmail, value);
    }

    public override Type SettingsType => typeof(TestSettingsWithValidation);

    protected override ITestSettingsWithValidation GetSettingsSnapshot() => new TestSettingsWithValidation
    {
        RequiredField = this.RequiredField,
        OutOfRangeValue = this.OutOfRangeValue,
        InvalidEmail = this.InvalidEmail,
    };

    protected override void UpdateProperties(ITestSettingsWithValidation source)
    {
        this.RequiredField = source.RequiredField;
        this.OutOfRangeValue = source.OutOfRangeValue;
        this.InvalidEmail = source.InvalidEmail;
    }

    protected override ITestSettingsWithValidation CreateDefaultSettings() => new TestSettingsWithValidation
    {
        RequiredField = null,
        OutOfRangeValue = -1,
        InvalidEmail = "not-an-email",
    };
}
