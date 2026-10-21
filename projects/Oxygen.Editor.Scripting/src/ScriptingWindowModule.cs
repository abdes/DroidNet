// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Managed.Assets.Diagnostics;

namespace Oxygen.Editor.Scripting;

/// <summary>
///     Script authoring in a workspace window's scope: the script facts and their problems, and the project's script
///     tooling.
/// </summary>
/// <remarks>
///     The script facts follow the window's asset catalog, so they live as long as it does. The project session owns
///     these services once it exists.
/// </remarks>
public sealed class ScriptingWindowModule : IScopeModule<IWindowLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.RegisterInstance(ScriptToolingPaths.Discover());
        scope.Register<ProjectScriptTooling>(Reuse.Singleton);
        scope.RegisterMany([typeof(IScripts), typeof(IAssetProblemSource)], typeof(ScriptFacts), Reuse.Singleton);
    }
}
