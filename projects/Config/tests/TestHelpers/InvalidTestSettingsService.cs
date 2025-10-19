// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace DroidNet.Config.Tests.TestHelpers;

/// <summary>
/// Concrete implementation of SettingsService for IInvalidTestSettings interface.
/// This service implements both the abstract SettingsService base class and the IInvalidTestSettings interface.
/// Used for testing validation failure scenarios.
/// </summary>
public sealed class InvalidTestSettingsService : SettingsService<IInvalidTestSettings>, IInvalidTestSettings
{
    private string? requiredField; // Invalid: null/empty
    private int outOfRangeValue = -1; // Invalid: out of range (should be 1-10)
    private string? invalidEmail = "not-an-email"; // Invalid: not an email

    public InvalidTestSettingsService(SettingsManager manager, ILoggerFactory? loggerFactory = null)
        : base(manager, loggerFactory)
    {
    }

    /// <inheritdoc/>
    public override string SectionName => "InvalidTestSettings";

    /// <inheritdoc/>
    protected override Type PocoType => typeof(InvalidTestSettings);

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

    protected override IInvalidTestSettings GetSettingsSnapshot()
    {
        return new InvalidTestSettings
        {
            RequiredField = this.RequiredField,
            OutOfRangeValue = this.OutOfRangeValue,
            InvalidEmail = this.InvalidEmail,
        };
    }

    protected override void UpdateProperties(IInvalidTestSettings source)
    {
        this.RequiredField = source.RequiredField;
        this.OutOfRangeValue = source.OutOfRangeValue;
        this.InvalidEmail = source.InvalidEmail;
    }

    protected override IInvalidTestSettings CreateDefaultSettings()
    {
        return new InvalidTestSettings
        {
            RequiredField = null,
            OutOfRangeValue = -1,
            InvalidEmail = "not-an-email",
        };
    }
}
