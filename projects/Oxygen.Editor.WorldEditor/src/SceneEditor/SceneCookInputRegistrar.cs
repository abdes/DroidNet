// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Hosting.WinUI;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>Tells the cook which saved scene file an open scene document edits and whether it has unsaved edits.</summary>
/// <param name="documents">The shared registry of open authoring inputs.</param>
/// <param name="hostingContext">The authoring UI dispatcher.</param>
public sealed partial class SceneCookInputRegistrar(ICookDocumentRegistry documents, HostingContext hostingContext)
{
    /// <summary>Registers an open scene document until it closes.</summary>
    /// <param name="tab">The scene document's tab, which carries its document and the change counters the cook compares.</param>
    /// <param name="commands">The owner of the scene's edits in progress.</param>
    /// <returns>The registration to dispose when the document closes.</returns>
    public IDisposable Register(SceneDocumentMetadata tab, ISceneDocumentCommandService commands)
    {
        ArgumentNullException.ThrowIfNull(tab);
        ArgumentNullException.ThrowIfNull(commands);
        var document = tab.Document ?? throw new ArgumentException("The scene tab has no document.", nameof(tab));
        var registration = new Registration(tab, commands);
        registration.Attach(documents.Register(document.File.Path, _ => hostingContext.Dispatcher.DispatchAsync(registration.AcquireRead)));
        return registration;
    }

    private sealed partial class Registration(SceneDocumentMetadata tab, ISceneDocumentCommandService commands) : IDisposable
    {
        private readonly global::Oxygen.Editor.Documents.Document document = tab.Document!;
        private ICookDocumentRegistration? registration;
        private string sourcePath = tab.Document!.File.Path;

        public void Attach(ICookDocumentRegistration value)
        {
            this.registration = value;
            this.document.StateChanged += this.OnStateChanged;
            this.registration.UpdateState(this.CaptureState());
        }

        public void Dispose()
        {
            this.document.StateChanged -= this.OnStateChanged;
            this.registration?.Dispose();
        }

        // Holds off replacing the scene while the cook reads its file, so the state it captured stays the scene's.
        public CookDocumentReadLease? AcquireRead()
            => this.document.AcquireReadLease() is { } lease
                ? new CookDocumentReadLease(this.CaptureState(), lease.Dispose)
                : null;

        private void OnStateChanged(object? sender, EventArgs args)
        {
            var state = this.CaptureState();
            if (!string.Equals(this.sourcePath, state.SourcePath, StringComparison.OrdinalIgnoreCase))
            {
                this.registration?.RelocateSource(state);
                this.sourcePath = state.SourcePath;
            }
            else
            {
                this.registration?.UpdateState(state);
            }
        }

        private CookDocumentState CaptureState()
        {
            // The tab's change counters follow the document; whichever hears of a change first brings them up to date.
            _ = tab.Refresh();
            var scene = (this.document.Content as SceneContent)?.Scene;
            return new CookDocumentState(
                this.document.File.Path,
                this.document.Title,
                tab.ChangeVersion,
                tab.SavedVersion,
                this.document.IsDirty || (scene is not null && commands.HasEditInProgress(scene)),
                this.document.Version.Sha256);
        }
    }
}
