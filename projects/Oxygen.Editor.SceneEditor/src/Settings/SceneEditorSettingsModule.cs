// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Data.Services;

namespace Oxygen.Editor.SceneEditor.Settings;

/// <summary>
///     The scene editor's preferences in the editor scope: transform gizmo snapping and space, per-user preferences
///     kept across projects, with serialized writes.
/// </summary>
/// <remarks>
///     The preferences stored per project belong to each window, through <see cref="SceneEditorModule" />.
/// </remarks>
public sealed class SceneEditorSettingsModule : IScopeModule<IApplicationLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.RegisterDelegate(
            resolver => new TransformToolSettingsService(
                resolver.Resolve<IEditorSettingsManager>(),
                resolver.Resolve<ILoggerFactory>()),
            Reuse.Singleton);
    }
}
