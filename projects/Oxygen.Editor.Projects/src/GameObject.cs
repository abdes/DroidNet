// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;

/// <summary>
/// The base class for any game object. Exposes a <see cref="Name" /> property and provides the required boilerplate for
/// implementing <see cref="INotifyPropertyChanged" /> for observable properties.
/// </summary>
public partial class GameObject : INotifyPropertyChanged
{
    private string name;

    protected delegate void ValidateValueCallback(object? value);

    public event PropertyChangedEventHandler? PropertyChanged;

    public required string Name
    {
        get => this.name;
        [MemberNotNull(nameof(name))]
        set => _ = this.ValidateAndSetField(ref this.name!, value, ValidateName);
    }

    public static void ValidateName(object? name)
    {
        if (string.IsNullOrWhiteSpace(name as string))
        {
            throw new ArgumentException(
                "a scene must have a name and it should not be only white spaces",
                nameof(name));
        }
    }

    protected virtual void OnPropertyChanged([CallerMemberName] string? propertyName = null)
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

    protected bool ValidateAndSetField<T>(
        ref T field,
        T value,
        ValidateValueCallback validator,
        [CallerMemberName]
        string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        validator.Invoke(value);

        field = value;
        this.OnPropertyChanged(propertyName);
        return true;
    }
}
