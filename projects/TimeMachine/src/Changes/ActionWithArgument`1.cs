// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Changes;

/// <summary>
/// Represents a change that involves executing an action with a specified argument.
/// </summary>
/// <typeparam name="TArgument">The type of the argument passed to the action.</typeparam>
/// <remarks>
/// This class is useful for encapsulating actions that need to be performed with a specific argument,
/// allowing them to be managed as part of the undo/redo system.
/// </remarks>
public class ActionWithArgument<TArgument>(Action<TArgument?> action, TArgument? argument) : Change
{
    /// <summary>
    /// Applies the change by executing the action with the specified argument.
    /// </summary>
    /// <example>
    /// <para><strong>Example Usage:</strong></para>
    /// <code><![CDATA[
    /// var action = new ActionWithArgument<string>(s => Console.WriteLine(s), "Hello, World!");
    /// action.Apply(); // Outputs: Hello, World!
    /// ]]></code>
    /// </example>
    public override void Apply() => action(argument);
}
