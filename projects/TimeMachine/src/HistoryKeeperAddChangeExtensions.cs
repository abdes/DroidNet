// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Linq.Expressions;
using DroidNet.TimeMachine.Changes;

namespace DroidNet.TimeMachine;

/// <summary>
/// Provides extension methods for adding changes to the <see cref="HistoryKeeper"/>.
/// </summary>
/// <remarks>
/// The <see cref="HistoryKeeperAddChangeExtensions"/> class provides a set of extension methods that simplify the process
/// of adding changes to the <see cref="HistoryKeeper"/>. These methods allow for adding changes with different types of
/// actions and arguments.
/// </remarks>
public static class HistoryKeeperAddChangeExtensions
{
    /// <summary>
    /// Adds a change that involves executing a lambda expression on a specified target.
    /// </summary>
    /// <typeparam name="TTarget">The type of the target on which the expression will be executed.</typeparam>
    /// <param name="undoManager">The <see cref="HistoryKeeper"/> instance to which the change will be added.</param>
    /// <param name="label">A label to identify the change.</param>
    /// <param name="target">The target object on which the expression will be executed.</param>
    /// <param name="selector">The lambda expression to execute on the target.</param>
    /// <example>
    /// <para><strong>Example Usage:</strong></para>
    /// <code><![CDATA[
    /// var historyKeeper = new HistoryKeeper(rootObject);
    /// historyKeeper.AddChange("ChangeLabel", targetObject, t => t.SomeMethod());
    /// ]]></code>
    /// </example>
    public static void AddChange<TTarget>(
        this HistoryKeeper undoManager,
        string label,
        TTarget target,
        Expression<Action<TTarget>> selector)
    {
        var action = new LambdaExpressionOnTarget<TTarget>(target, selector) { Key = label };
        undoManager.AddChange(action);
    }

    /// <summary>
    /// Adds a change that involves executing an action with a specified argument.
    /// </summary>
    /// <typeparam name="TArgument">The type of the argument passed to the action.</typeparam>
    /// <param name="undoManager">The <see cref="HistoryKeeper"/> instance to which the change will be added.</param>
    /// <param name="label">A label to identify the change.</param>
    /// <param name="selector">The action to execute with the specified argument.</param>
    /// <param name="argument">The argument to pass to the action.</param>
    /// <example>
    /// <para><strong>Example Usage:</strong></para>
    /// <code><![CDATA[
    /// var historyKeeper = new HistoryKeeper(rootObject);
    /// historyKeeper.AddChange("ChangeLabel", arg => Console.WriteLine(arg), "Hello, World!");
    /// ]]></code>
    /// </example>
    public static void AddChange<TArgument>(
        this HistoryKeeper undoManager,
        string label,
        Action<TArgument?> selector,
        TArgument argument)
    {
        var action = new ActionWithArgument<TArgument?>(selector, argument) { Key = label };
        undoManager.AddChange(action);
    }

    /// <summary>
    /// Adds a change that involves executing a simple action.
    /// </summary>
    /// <param name="undoManager">The <see cref="HistoryKeeper"/> instance to which the change will be added.</param>
    /// <param name="label">A label to identify the change.</param>
    /// <param name="selector">The action to execute.</param>
    /// <example>
    /// <para><strong>Example Usage:</strong></para>
    /// <code><![CDATA[
    /// var historyKeeper = new HistoryKeeper(rootObject);
    /// historyKeeper.AddChange("ChangeLabel", () => Console.WriteLine("Action executed"));
    /// ]]></code>
    /// </example>
    public static void AddChange(this HistoryKeeper undoManager, string label, Action selector)
    {
        var action = new SimpleAction(selector) { Key = label };
        undoManager.AddChange(action);
    }
}
