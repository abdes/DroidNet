// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Linq.Expressions;

namespace DroidNet.TimeMachine.Changes;

/// <summary>
/// Represents a change that involves executing a lambda expression on a specified target.
/// </summary>
/// <typeparam name="TTarget">The type of the target on which the expression will be executed.</typeparam>
/// <remarks>
/// The <see cref="LambdaExpressionOnTarget{TTarget}"/> class allows a lambda expression to be compiled and executed
/// on a specified target, making it useful for scenarios where actions need to be performed on specific objects
/// within the undo/redo system.
/// </remarks>
public class LambdaExpressionOnTarget<TTarget>(TTarget target, Expression<Action<TTarget>> expression)
    : TargetedChange<TTarget>(target)
{
    /// <summary>
    /// Applies the change by compiling and executing the lambda expression on the target.
    /// </summary>
    /// <remarks>
    /// This method compiles the provided lambda expression and executes it on the target object.
    /// </remarks>
    /// <example>
    /// <para><strong>Example Usage:</strong></para>
    /// <code><![CDATA[
    /// var target = new TargetObject();
    /// var expression = new Expression<Action<TargetObject>>(t => t.PerformAction());
    /// var change = new LambdaExpressionOnTarget<TargetObject>(target, expression);
    /// change.Apply(); // Executes PerformAction on the target object
    /// ]]></code>
    /// </example>
    public override void Apply()
    {
        var action = expression.Compile();
        action(this.Target);
    }
}
