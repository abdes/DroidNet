// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Changes;

/// <summary>
/// Represents an abstract base class for a change that can be applied.
/// </summary>
/// <remarks>
/// The <see cref="Change"/> class provides a foundation for implementing specific changes that can be managed
/// within the undo/redo system. Derived classes must implement the <see cref="Apply"/> method to define the
/// specific behavior of the change.
/// </remarks>
public abstract class Change : IChange
{
    /// <summary>
    /// Gets the key that identifies the change.
    /// </summary>
    /// <value>
    /// An object that uniquely identifies the change.
    /// </value>
    /// <remarks>
    /// The <see cref="Key"/> property is used to identify the change within the undo/redo system. It is required
    /// and must be set by derived classes.
    /// </remarks>
    public required object Key { get; init; }

    /// <summary>
    /// Applies the change.
    /// </summary>
    /// <remarks>
    /// Derived classes must implement this method to define the specific behavior of the change.
    /// </remarks>
    public abstract void Apply();

    /// <summary>
    /// Applies the change asynchronously.
    /// </summary>
    /// <param name="cancellationToken">A token that can be used to cancel the operation.</param>
    /// <returns>A task-like object that represents the asynchronous operation.</returns>
    /// <remarks>
    /// The default implementation delegates to <see cref="Apply"/>. It exists, although we have a default interface
    /// method in IChange, so that derived classes can override async behavior cleanly and predictably.
    /// </remarks>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MA0042:Do not use blocking calls in an async method", Justification = "this is the wrapper that invokes the non-async change")]
    public virtual ValueTask ApplyAsync(CancellationToken cancellationToken = default)
    {
        _ = cancellationToken;
        this.Apply();
        return ValueTask.CompletedTask;
    }

    /// <summary>
    /// Returns a string representation of the change.
    /// </summary>
    /// <returns>
    /// A string that represents the change, typically the string representation of the <see cref="Key"/>.
    /// </returns>
    public override string? ToString() => this.Key.ToString();
}
