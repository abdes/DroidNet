// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Mvvm;

/// <summary>
/// Represents the data provided with the <see cref="IViewFor{T}.ViewModelChanged" /> event.
/// </summary>
/// <typeparam name="T">The ViewModel type.</typeparam>
/// <param name="oldValue">The old value of the <see cref="IViewFor{T}.ViewModel" /> property.</param>
public class ViewModelChangedEventArgs<T>(T? oldValue) : EventArgs
    where T : class
{
    /// <summary>
    /// Gets the old value of the <see cref="IViewFor{T}.ViewModel" /> property.
    /// </summary>
    public T? OldValue { get; } = oldValue;
}
