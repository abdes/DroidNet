// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Microsoft.Extensions.Hosting;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor;

/// <summary>
///     The embedded engine in the editor scope: one engine for every window, and its shutdown with the host.
/// </summary>
/// <remarks>
///     The engine's startup settings are a settings service, which <see cref="EditorModule" /> adds with the host's
///     other settings.
/// </remarks>
internal sealed class EngineModule : IScopeModule<IApplicationLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<IEngineService, EngineService>(Reuse.Singleton);
        scope.Register<EngineShutdownService>(Reuse.Singleton);
        scope.RegisterDelegate<IHostedService>(resolver => resolver.Resolve<EngineShutdownService>(), Reuse.Singleton);
    }
}
