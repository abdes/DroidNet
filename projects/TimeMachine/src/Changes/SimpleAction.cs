// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Changes;

/// <summary>
/// Represents a change that involves executing a simple action.
/// </summary>
/// <remarks>
/// The <see cref="SimpleAction"/> class allows an action to be encapsulated as a change, making it easy to manage
/// within the undo/redo system. This is useful for scenarios where a single action needs to be performed and tracked
/// as part of the undo/redo history.
/// </remarks>
public class SimpleAction(Action action) : Change
{
    /// <summary>
    /// Applies the change by executing the action.
    /// </summary>
    /// <remarks>
    /// This method executes the provided action.
    /// </remarks>
    /// <example>
    /// <para><strong>Example Usage:</strong></para>
    /// <code><![CDATA[
    /// var action = new SimpleAction(() => Console.WriteLine("Action executed"));
    /// action.Apply(); // Outputs: Action executed
    /// ]]></code>
    /// </example>
    public override void Apply() => action();
}
