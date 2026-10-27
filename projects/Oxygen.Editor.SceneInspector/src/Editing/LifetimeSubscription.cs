// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.SceneInspector.Editing;

/// <summary>
/// A subscription that ends when its owner ends it or when a lifetime, such as the project session's, ends: whichever
/// comes first.
/// </summary>
internal static class LifetimeSubscription
{
    /// <summary>Subscribes to a sequence until the returned source is cancelled or the lifetime ends.</summary>
    /// <typeparam name="T">The sequence's element type.</typeparam>
    /// <param name="source">The sequence.</param>
    /// <param name="onNext">Receives each element.</param>
    /// <param name="onError">Receives the error that ends the sequence.</param>
    /// <param name="lifetime">The lifetime the subscription ends with.</param>
    /// <returns>The source that ends the subscription; the caller ends it with <see cref="End"/>.</returns>
    public static CancellationTokenSource Subscribe<T>(IObservable<T> source, Action<T> onNext, Action<Exception> onError, CancellationToken lifetime)
    {
        ArgumentNullException.ThrowIfNull(source);
        var subscription = CancellationTokenSource.CreateLinkedTokenSource(lifetime);
        source.Subscribe(onNext, onError, subscription.Token);
        return subscription;
    }

    /// <summary>Subscribes to a sequence until the returned source is cancelled or the lifetime ends.</summary>
    /// <typeparam name="T">The sequence's element type.</typeparam>
    /// <param name="source">The sequence.</param>
    /// <param name="onNext">Receives each element.</param>
    /// <param name="lifetime">The lifetime the subscription ends with.</param>
    /// <returns>The source that ends the subscription; the caller ends it with <see cref="End"/>.</returns>
    public static CancellationTokenSource Subscribe<T>(IObservable<T> source, Action<T> onNext, CancellationToken lifetime)
    {
        ArgumentNullException.ThrowIfNull(source);
        var subscription = CancellationTokenSource.CreateLinkedTokenSource(lifetime);
        source.Subscribe(onNext, subscription.Token);
        return subscription;
    }

    /// <summary>Ends a subscription, if any, and forgets it.</summary>
    /// <param name="subscription">The subscription's source; <see langword="null"/> afterwards.</param>
    public static void End(ref CancellationTokenSource? subscription)
    {
        subscription?.Cancel();
        subscription?.Dispose();
        subscription = null;
    }
}
