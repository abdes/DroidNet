// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;

namespace DroidNet.Config.Tests.Helpers;

/// <summary>
/// Test settings with validation errors for testing validation scenarios.
/// </summary>
[ExcludeFromCodeCoverage]
public class InvalidTestSettings : IInvalidTestSettings
{
    private string? requiredField;
    private int outOfRangeValue = 999;
    private string? invalidEmail = "not-an-email";

    public event PropertyChangedEventHandler? PropertyChanged;

    [Required]
    public string? RequiredField
    {
        get => this.requiredField;
        set => this.SetField(ref this.requiredField, value);
    }

    [Range(1, 10)]
    public int OutOfRangeValue
    {
        get => this.outOfRangeValue;
        set => this.SetField(ref this.outOfRangeValue, value);
    }

    [EmailAddress]
    public string? InvalidEmail
    {
        get => this.invalidEmail;
        set => this.SetField(ref this.invalidEmail, value);
    }

    protected void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

    protected bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        this.OnPropertyChanged(propertyName);
        return true;
    }
}
