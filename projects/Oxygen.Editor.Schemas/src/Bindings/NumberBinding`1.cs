// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Schemas.Bindings;

/// <summary>
/// A property binding for a numeric property, edited by number controls that work in <see langword="double"/>.
/// </summary>
/// <typeparam name="T">The numeric type the property stores, such as <see langword="float"/>.</typeparam>
/// <remarks>
/// <see cref="Number"/> is the view's value; <see cref="PropertyBinding{T}.Value"/> stays in the stored type, and an
/// edited number converts to it once, when it is requested.
/// </remarks>
/// <param name="descriptor">The descriptor.</param>
public sealed class NumberBinding<T>(PropertyDescriptor<T> descriptor) : PropertyBinding<T>(descriptor)
    where T : INumber<T>
{
    /// <summary>
    /// Gets or sets the value as a <see langword="double"/>. Setting it requests the value converted to
    /// <typeparamref name="T"/>, saturating at its limits.
    /// </summary>
    public double Number
    {
        get => double.CreateChecked(this.Value);
        set => this.Value = T.CreateSaturating(value);
    }

    /// <inheritdoc/>
    protected override void OnValueChanged()
    {
        base.OnValueChanged();
        this.OnPropertyChanged(nameof(this.Number));
    }
}
