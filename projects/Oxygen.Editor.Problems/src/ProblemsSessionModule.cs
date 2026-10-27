// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Managed.Assets.Diagnostics;

namespace Oxygen.Editor.Problems;

/// <summary>The problems in a project session's scope: the list of the session's problem sources.</summary>
public sealed class ProblemsSessionModule : IScopeModule<ISessionLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // The session's own sources, in registration order: a collection resolved here sees only the session's.
        scope.RegisterDelegate<IAssetProblemSources>(
            resolver => new AssetProblemSources(resolver.ResolveMany<IAssetProblemSource>()),
            Reuse.Singleton);
    }
}
