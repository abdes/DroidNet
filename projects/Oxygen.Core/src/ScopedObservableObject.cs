// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Oxygen.Core;

/// <summary>
/// Base class for observable objects with support for scoped notification suppression.
/// </summary>
/// <remarks>
/// Provides <see cref="INotifyPropertyChanged"/> and <see cref="INotifyPropertyChanging"/>
/// implementation with the ability to temporarily suppress notifications using a RAII pattern
/// via <see cref="SuppressNotifications"/>.
/// </remarks>
public abstract class ScopedObservableObject : INotifyPropertyChanged, INotifyPropertyChanging
{
    private int suppressNotificationCount;

    /// <inheritdoc/>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <inheritdoc/>
    public event PropertyChangingEventHandler? PropertyChanging;

    /// <summary>
    /// Gets a value indicating whether property change notifications are currently enabled.
    /// </summary>
    public bool AreNotificationsEnabled => this.suppressNotificationCount == 0;

    /// <summary>
    /// Suppresses property change notifications until the returned <see cref="IDisposable"/> is disposed.
    /// </summary>
    /// <returns>
    /// An <see cref="IDisposable"/> that, when disposed, re-enables notifications.
    /// </returns>
    /// <remarks>
    /// This method uses a RAII pattern. Notifications are suppressed when called and automatically
    /// re-enabled when the returned object is disposed. Calls can be nested; notifications are only
    /// re-enabled when all suppressions are released.
    /// <example>
    /// <code>
    /// using (SuppressNotifications())
    /// {
    ///     Property1 = value1; // No notification
    ///     Property2 = value2; // No notification
    /// } // Notifications re-enabled here
    /// </code>
    /// </example>
    /// </remarks>
    public IDisposable SuppressNotifications()
    {
        Interlocked.Increment(ref this.suppressNotificationCount);
        return new NotificationSuppressor(() => Interlocked.Decrement(ref this.suppressNotificationCount));
    }

    /// <summary>
    /// Raises the <see cref="PropertyChanged"/> event.
    /// </summary>
    /// <param name="propertyName">The name of the property that changed.</param>
    protected virtual void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        if (this.suppressNotificationCount > 0)
        {
            return;
        }

        this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }

    /// <summary>
    /// Raises the <see cref="PropertyChanging"/> event.
    /// </summary>
    /// <param name="propertyName">The name of the property that is changing.</param>
    protected virtual void OnPropertyChanging([CallerMemberName] string? propertyName = null)
    {
        if (this.suppressNotificationCount > 0)
        {
            return;
        }

        this.PropertyChanging?.Invoke(this, new PropertyChangingEventArgs(propertyName));
    }

    /// <summary>
    /// Sets the property and raises property change events if the value has changed.
    /// </summary>
    /// <typeparam name="T">The type of the property.</typeparam>
    /// <param name="property">The property to set.</param>
    /// <param name="value">The new value.</param>
    /// <param name="propertyName">The name of the property that changed.</param>
    /// <returns><see langword="true"/> if the value has changed; otherwise, <see langword="false"/>.</returns>
    protected bool SetProperty<T>(ref T property, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(property, value))
        {
            return false;
        }

        this.OnPropertyChanging(propertyName);
        property = value;
        this.OnPropertyChanged(propertyName);
        return true;
    }

    /// <summary>
    /// RAII helper for notification suppression.
    /// </summary>
    private readonly struct NotificationSuppressor(Action onDispose) : IDisposable
    {
        /// <inheritdoc/>
        public void Dispose() => onDispose();
    }
}
