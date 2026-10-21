// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Diagnostics;

/// <summary>
///     The editor's diagnostics in the editor scope: the status reducer, the exception adapter and the operation-result
///     store, whose results reach the output log from startup.
/// </summary>
internal sealed class DiagnosticsModule : IScopeModule<IApplicationLayer>
{
    // The output log shows every operation result published from startup.
    private static readonly Type[] Activated = [typeof(OperationResultOutputLogAdapter)];

    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => Activated;

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<IStatusReducer, OperationStatusReducer>(Reuse.Singleton);
        scope.Register<IExceptionDiagnosticAdapter, ExceptionDiagnosticAdapter>(Reuse.Singleton);
        scope.Register<OperationResultStore>(Reuse.Singleton);
        scope.RegisterDelegate<IOperationResultStore>(
            resolverContext => resolverContext.Resolve<OperationResultStore>(),
            Reuse.Singleton);
        scope.RegisterDelegate<IOperationResultPublisher>(
            resolverContext => resolverContext.Resolve<OperationResultStore>(),
            Reuse.Singleton);
        scope.Register<OperationResultOutputLogAdapter>(Reuse.Singleton);
    }
}
