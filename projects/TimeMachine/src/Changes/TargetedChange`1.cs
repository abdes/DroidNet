// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Changes;

/// <summary>
/// Represents an abstract base class for a change that targets a specific object.
/// </summary>
/// <typeparam name="TTarget">The type of the target object.</typeparam>
/// <remarks>
/// The <see cref="TargetedChange{TTarget}"/> class provides a foundation for implementing changes that are applied
/// to a specific target object. Derived classes must implement the <see cref="Change.Apply"/> method to define the
/// specific behavior of the change.
/// </remarks>
public abstract class TargetedChange<TTarget>(TTarget target) : Change
{
    /// <summary>
    /// Gets the target object for this change.
    /// </summary>
    /// <value>
    /// The target object on which the change will be applied.
    /// </value>
    /// <remarks>
    /// The <see cref="Target"/> property holds the reference to the target object that the change will be applied to.
    /// </remarks>
    public TTarget Target { get; } = target;
}
