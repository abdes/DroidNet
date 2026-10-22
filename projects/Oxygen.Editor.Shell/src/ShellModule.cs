// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.Shell;

/// <summary>
///     The Shell's services in the editor scope: the close of every window and the editor's exit, and the questions that
///     documents ask of a window. The tabs of a session's windows are the session's.
/// </summary>
/// <remarks>
///     The close and exit service is the only handler of Aura's window closing. The participants of the editor scope
///     are registered as <see cref="IWindowCloseParticipant" /> by the composition root; a window's participants join
///     while the window lives. The application creates the service with its UI. The questions take the
///     window they act on with each call, so they serve every window.
/// </remarks>
public sealed class ShellModule : IScopeModule<IApplicationLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<CloseAndExit>(Reuse.Singleton);
        scope.RegisterMapping<ICloseAndExit, CloseAndExit>();
        scope.Register<IDocumentClosePrompt, DocumentClosePrompt>(Reuse.Singleton);
        scope.Register<IDocumentConfirmations, DocumentConfirmations>(Reuse.Singleton);
    }
}
