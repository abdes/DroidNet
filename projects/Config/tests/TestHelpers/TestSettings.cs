// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;

namespace DroidNet.Config.Tests.TestHelpers;

/// <summary>
/// Test settings implementation for general testing scenarios.
/// </summary>
[ExcludeFromCodeCoverage]
public class TestSettings : ITestSettings
{
    private string name = "Default";
    private int value = 42;
    private bool isEnabled = true;
    private string? description;

    public event PropertyChangedEventHandler? PropertyChanged;

    [Required]
    [StringLength(100, MinimumLength = 1)]
    public string Name
    {
        get => this.name;
        set => this.SetField(ref this.name, value);
    }

    [Range(0, 1000)]
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

    protected void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }

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
