// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Editor.LivePreview.Scripting;
using Oxygen.Editor.Scripting;

namespace Oxygen.Editor.LivePreview;

/// <summary>
///     The engine adapters of the editor scope: the engine's script analyzer, which the script facts of every workspace
///     use.
/// </summary>
public sealed class LivePreviewModule : IScopeModule<IApplicationLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<IScriptAnalyzer, NativeScriptAnalyzer>(Reuse.Singleton);
    }
}
