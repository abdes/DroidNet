// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Managed.Assets.Diagnostics;

namespace Oxygen.Editor.Input;

/// <summary>
///     Input authoring in a project session's scope: the input facts and their problems.
/// </summary>
/// <remarks>
///     The input facts follow the session's asset catalog and its content mounts, and end with the session.
/// </remarks>
public sealed class InputSessionModule : IScopeModule<ISessionLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.RegisterMany([typeof(IInputSources), typeof(IAssetProblemSource)], typeof(InputFacts), Reuse.Singleton);
    }
}
