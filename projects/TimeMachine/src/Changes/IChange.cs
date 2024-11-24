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
}
