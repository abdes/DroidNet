// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Changes;

/// <summary>
/// Represents a change that can be applied within the undo/redo system.
/// </summary>
/// <remarks>
/// The <see cref="IChange"/> interface defines the contract for changes that can be managed by the undo/redo system.
/// Implementing this interface allows changes to be uniquely identified and applied.
/// </remarks>
public interface IChange
{
    /// <summary>
    /// Gets the key that identifies the change.
    /// </summary>
    /// <value>
    /// An object that uniquely identifies the change.
    /// </value>
    /// <remarks>
    /// The <see cref="Key"/> property is used to identify the change within the undo/redo system. It should be unique
    /// to ensure that each change can be distinguished from others.
    /// </remarks>
    public object Key { get; }

    /// <summary>
    /// Applies the change.
    /// </summary>
    /// <remarks>
    /// Implement this method to define the specific behavior of the change when it is applied.
    /// </remarks>
    public void Apply();

    /// <summary>
    /// Applies the change asynchronously.
    /// </summary>
    /// <param name="cancellationToken">A token that can be used to cancel the operation.</param>
    /// <returns>A task-like object that represents the asynchronous operation.</returns>
    /// <remarks>
    /// The default implementation is for backward compatibility and "interface-only" implementers,
    /// and delegates to <see cref="Apply"/>.
    /// </remarks>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MA0042:Do not use blocking calls in an async method", Justification = "this is the wrapper that invokes the non-async change")]
    public ValueTask ApplyAsync(CancellationToken cancellationToken = default)
    {
        _ = cancellationToken;
        this.Apply();
        return ValueTask.CompletedTask;
    }
}
