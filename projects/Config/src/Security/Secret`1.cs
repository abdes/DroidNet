// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;

namespace DroidNet.Config.Security;

/// <summary>
/// Wrapper for sensitive configuration values that require encryption.
/// </summary>
/// <typeparam name="T">The type of the secret value.</typeparam>
/// <remarks>
/// Initializes a new instance of the <see cref="Secret{T}"/> class.
/// </remarks>
/// <param name="value">The secret value to wrap.</param>
public sealed class Secret<T>(T value) : IEquatable<Secret<T>>, IDisposable
{
    private readonly T value = value ?? throw new ArgumentNullException(nameof(value));
    private bool disposed;

    /// <summary>
    /// Gets the secret value. This should only be used when the value is actually needed.
    /// </summary>
    /// <returns>The secret value.</returns>
    /// <exception cref="ObjectDisposedException">Thrown when the secret has been disposed.</exception>
    public T Value
    {
        get
        {
            this.ThrowIfDisposed();
            return this.value;
        }
    }

    /// <summary>
    /// Gets a value indicating whether the secret has a non-null, non-empty value.
    /// </summary>
    public bool HasValue
    {
        get
        {
            if (this.disposed)
            {
                return false;
            }

            return this.value switch
            {
                null => false,
                string s => !string.IsNullOrEmpty(s),
                _ => true,
            };
        }
    }

    /// <summary>
    /// Unwraps the secret value.
    /// </summary>
    /// <returns>The underlying value.</returns>
    public T Unwrap() => this.Value;

    /// <summary>
    /// Determines whether the specified object is equal to the current secret.
    /// This comparison is based on the underlying values.
    /// </summary>
    /// <param name="other">The secret to compare with the current secret.</param>
    /// <returns>true if the specified secret is equal to the current secret; otherwise, false.</returns>
    public bool Equals(Secret<T>? other)
    {
        if (other is null)
        {
            return false;
        }

        if (ReferenceEquals(this, other))
        {
            return true;
        }

        if (this.disposed || other.disposed)
        {
            return false;
        }

        return EqualityComparer<T>.Default.Equals(this.value, other.value);
    }

    /// <summary>
    /// Determines whether the specified object is equal to the current secret.
    /// </summary>
    /// <param name="obj">The object to compare with the current secret.</param>
    /// <returns>true if the specified object is equal to the current secret; otherwise, false.</returns>
    public override bool Equals(object? obj)
    {
        return this.Equals(obj as Secret<T>);
    }

    /// <summary>
    /// Serves as the default hash function.
    /// </summary>
    /// <returns>A hash code for the current secret.</returns>
    public override int GetHashCode()
    {
        if (this.disposed)
        {
            return 0;
        }

        Debug.Assert(this.value is not null, "Value should not be null here.");
        return EqualityComparer<T>.Default.GetHashCode(this.value);
    }

    /// <summary>
    /// Returns a string representation that doesn't expose the secret value.
    /// </summary>
    /// <returns>A masked string representation.</returns>
    public override string ToString()
    {
        if (this.disposed)
        {
            return "[DISPOSED]";
        }

        return this.HasValue ? "[SECRET]" : "[EMPTY]";
    }

    /// <summary>
    /// Disposes the secret and clears sensitive data from memory if possible.
    /// </summary>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        // For string types, try to clear the memory if possible
        // Note: This is limited by .NET's immutable string behavior
        if (this.value is IDisposable disposable)
        {
            disposable.Dispose();
        }

        this.disposed = true;
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(this.disposed, nameof(Secret<>));
}
