// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine;

using System.Linq.Expressions;
using DroidNet.TimeMachine.Changes;

public static class HistoryKeeperAddChangeExtensions
{
    public static void AddChange<TTarget>(
        this HistoryKeeper undoManager,
        string label,
        TTarget target,
        Expression<Action<TTarget>> selector)
    {
        var action = new LambdaExpressionOnTarget<TTarget>(target, selector) { Key = label };
        undoManager.AddChange(action);
    }

    public static void AddChange<TArgument>(
        this HistoryKeeper undoManager,
        string label,
        Action<TArgument?> selector,
        TArgument argument)
    {
        var action = new ActionWithArgument<TArgument?>(selector, argument) { Key = label };
        undoManager.AddChange(action);
    }

    public static void AddChange(this HistoryKeeper undoManager, string label, Action selector)
    {
        var action = new SimpleAction(selector) { Key = label };
        undoManager.AddChange(action);
    }
}
