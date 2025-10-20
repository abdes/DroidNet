// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;

namespace DroidNet.Config.Tests.Helpers;

/// <summary>
/// Alternative test settings implementation for multi-service testing.
/// </summary>
[ExcludeFromCodeCoverage]
public class AlternativeTestSettings : IAlternativeTestSettings
{
    private string theme = "Light";
    private int fontSize = 14;
    private bool autoSave = true;

    public event PropertyChangedEventHandler? PropertyChanged;

    public string Theme
    {
        get => this.theme;
        set => this.SetField(ref this.theme, value);
    }

    [Range(8, 72)]
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
