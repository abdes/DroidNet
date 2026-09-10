// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Services;

/// <summary>Captures immutable runtime targets for feature operations and reduces managed outcomes.</summary>
public sealed partial class SceneEngineSync
{
    private static SyncOutcome FromRuntimeOutcome(
        RuntimeCommandResult result, string operationKind, AffectedScope scope, string rejectedCode, string failedCode)
        => result.Status switch
        {
            RuntimeCommandStatus.Accepted => Accepted(operationKind, scope),
            RuntimeCommandStatus.Cancelled => Cancelled(operationKind, scope),
            RuntimeCommandStatus.Unavailable when result.Exception is NotSupportedException or NotImplementedException
                => Unsupported(operationKind, scope, rejectedCode, result.Message ?? "The runtime capability is unavailable.", result.Exception),
            RuntimeCommandStatus.Unavailable => RuntimeWorldUnavailable(operationKind, scope, result.Exception),
            RuntimeCommandStatus.Rejected => Rejected(operationKind, scope, rejectedCode, result.Message ?? "Runtime rejected the command.", result.Exception),
            _ => Failed(operationKind, scope, failedCode, result.Message ?? "Runtime command failed.", result.Exception),
        };

    private sealed class DocumentLifetime
    {
        public Guid Id { get; } = Guid.NewGuid();
    }

    // This local dispatch context keeps the captured target and cancellation token together.
    // It owns no native objects and does not resolve a later active scene during dispatch.
    private sealed record WorldDispatch(IRuntimeWorldCommands Commands, RuntimeSceneTarget Target, CancellationToken CancellationToken)
    {
        public void Execute(RuntimeWorldCommand command)
        {
            var result = this.Commands.Execute(new RuntimeWorldRequest(Guid.NewGuid(), this.Target, command), this.CancellationToken);
            if (!result.Succeeded)
            {
                throw new RuntimeDispatchException(result);
            }
        }
    }

    // Legacy multi-command orchestration uses exceptions to stop a failed projection.
    // The runtime boundary itself always returns an explicit managed result.
    private sealed class RuntimeDispatchException(RuntimeCommandResult result) : InvalidOperationException(result.Message, result.Exception)
    {
        public RuntimeDispatchException()
            : this(new RuntimeCommandResult(Guid.Empty, Guid.Empty, RuntimeCommandStatus.Failed))
        {
        }

        public RuntimeDispatchException(string? message)
            : this(new RuntimeCommandResult(Guid.Empty, Guid.Empty, RuntimeCommandStatus.Failed, message))
        {
        }

        public RuntimeDispatchException(string? message, Exception? innerException)
            : this(new RuntimeCommandResult(Guid.Empty, Guid.Empty, RuntimeCommandStatus.Failed, message, innerException))
        {
        }

        public RuntimeCommandResult Result { get; } = result;
    }
}
