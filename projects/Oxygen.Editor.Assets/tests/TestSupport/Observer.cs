// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Assets.TestSupport;

/// <summary>Forwards each value of an observable to a callback and rethrows its errors.</summary>
/// <typeparam name="T">The observed value type.</typeparam>
/// <param name="onNext">The callback receiving each value.</param>
internal sealed class Observer<T>(Action<T> onNext) : IObserver<T>
{
    /// <inheritdoc />
    public void OnCompleted()
    {
    }

    /// <inheritdoc />
    public void OnError(Exception error) => throw error;

    /// <inheritdoc />
    public void OnNext(T value) => onNext(value);
}
