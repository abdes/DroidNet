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
    /// Returns a string representation of the change.
    /// </summary>
    /// <returns>
    /// A string that represents the change, typically the string representation of the <see cref="Key"/>.
    /// </returns>
    public override string? ToString() => this.Key.ToString();
}
