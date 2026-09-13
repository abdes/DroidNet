// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneExplorer.Services;

namespace Oxygen.Editor.World.Services;

/// <summary>Follows active document and selection lifetimes without cooking from change notifications.</summary>
public sealed partial class SceneContentDemandService
{
    private void Subscribe()
    {
        this.messenger.Register<SceneAuthoringLoadedMessage>(this, (_, message) => this.Dispatch(() => this.Activate(message.Scene, message.Metadata)));
        this.messenger.Register<SceneNodeSelectionChangedMessage>(this, (_, message) =>
        {
            var selection = message.SelectedEntities.Select(static node => node.Id).ToHashSet();
            this.Dispatch(() =>
            {
                foreach (var demand in this.demands.Where(demand => demand.Targets is { } targets && !targets.SetEquals(selection)).ToArray())
                {
                    demand.Cancel();
                }
            });
        });
        this.documents.DocumentActivated += this.OnActivated;
        this.documents.DocumentClosed += this.OnClosed;
        this.documents.DocumentMetadataChanged += this.OnMetadataChanged;
        this.explorer.AuthoringChanged += this.OnAuthoringChanged;
    }

    private void Unsubscribe()
    {
        this.messenger.UnregisterAll(this);
        this.documents.DocumentActivated -= this.OnActivated;
        this.documents.DocumentClosed -= this.OnClosed;
        this.documents.DocumentMetadataChanged -= this.OnMetadataChanged;
        this.explorer.AuthoringChanged -= this.OnAuthoringChanged;
    }

    private void Activate(Scene candidate, SceneDocumentMetadata owner)
    {
        if (this.documents.GetActiveDocumentId(this.windowId) != owner.DocumentId
            || !this.documents.GetOpenDocuments(this.windowId).Any(document => ReferenceEquals(document, owner))
            || (ReferenceEquals(this.scene, candidate) && ReferenceEquals(this.metadata, owner)))
        {
            return;
        }

        this.Reset();
        this.scene = candidate;
        this.metadata = owner;
        this.ObserveReferences();
        foreach (var (uri, kind) in References(candidate).Select(static reference => (reference.uri, reference.kind)).Distinct())
        {
            this.StartDemand(uri, kind, targets: null);
        }
    }

    private void OnActivated(object? sender, DocumentActivatedEventArgs args)
    {
        if (args.WindowId != this.windowId)
        {
            return;
        }

        this.Dispatch(() =>
        {
            var owner = this.documents.GetOpenDocuments(this.windowId).OfType<SceneDocumentMetadata>().FirstOrDefault(document => document.DocumentId == args.DocumentId);
            if (owner is not null && this.sceneSync.GetDocumentScene(owner) is { } loaded)
            {
                this.Activate(loaded, owner);
            }
            else
            {
                this.Reset();
            }
        });
    }

    private void OnClosed(object? sender, DocumentClosedEventArgs args)
    {
        if (args.WindowId == this.windowId)
        {
            this.Dispatch(() =>
            {
                if (ReferenceEquals(args.Metadata, this.metadata))
                {
                    this.Reset();
                }
            });
        }
    }

    private void OnMetadataChanged(object? sender, DocumentMetadataChangedEventArgs args)
    {
        if (args.WindowId == this.windowId)
        {
            this.Dispatch(this.InvalidateObsoleteDemands);
        }
    }

    private void OnAuthoringChanged(object? sender, SceneAuthoringChangedEventArgs args) => this.Dispatch(this.RefreshReferenceObservers);

    private void InvalidateObsoleteDemands()
    {
        foreach (var demand in this.demands.Where(demand => !this.IsCurrent(demand)).ToArray())
        {
            demand.Cancel();
        }
    }

    private void Reset()
    {
        this.RemoveReferenceObservers();
        foreach (var demand in this.demands.ToArray())
        {
            demand.Cancel();
        }

        this.scene = null;
        this.metadata = null;
    }

    private void Dispatch(Action action)
    {
        void Apply()
        {
            if (!this.disposed)
            {
                action();
            }
        }

        if (this.hosting.Dispatcher.HasThreadAccess)
        {
            Apply();
        }
        else
        {
            _ = this.hosting.Dispatcher.TryEnqueue(Apply);
        }
    }
}
