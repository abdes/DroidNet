// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Managed.Assets.Diagnostics;

namespace Oxygen.Editor.Input;

/// <summary>
///     Input authoring in a workspace window's scope: the input facts and their problems.
/// </summary>
/// <remarks>
///     The input facts follow the window's asset catalog, so they live as long as it does. The project session owns
///     them once it exists.
/// </remarks>
public sealed class InputWindowModule : IScopeModule<IWindowLayer>
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
