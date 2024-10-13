// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Changes;

using System.Linq.Expressions;

public class LambdaExpressionOnTarget<TTarget>(TTarget target, Expression<Action<TTarget>> expression)
    : TargetedChange<TTarget>(target)
{
    public override void Apply()
    {
        var action = expression.Compile();
        action(this.Target);
    }
}
