// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;

namespace Oxygen.Editor.World.Services;

/// <summary>Follows active document and selection lifetimes without cooking from change notifications.</summary>
public sealed partial class SceneContentDemandService
{
    private void Subscribe()
    {
        this.messenger.Register<SceneAuthoringLoadedMessage>(this, (_, message) => this.Dispatch(() => this.Activate(message.Scene, message.Document)));
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
        this.documentArea.ActiveDocumentChanged += this.OnActiveDocumentChanged;
    }

    private void Unsubscribe()
    {
        this.messenger.UnregisterAll(this);
        this.documentArea.ActiveDocumentChanged -= this.OnActiveDocumentChanged;
        this.Follow(null);
    }

    private void Activate(Scene candidate, Document owner)
    {
        if (!ReferenceEquals(this.documentArea.ActiveDocument, owner)
            || (ReferenceEquals(this.scene, candidate) && ReferenceEquals(this.document, owner)))
        {
            return;
        }

        this.Reset();
        this.scene = candidate;
        this.Follow(owner);
        this.ObserveReferences();
        foreach (var (uri, kind) in References(candidate).Select(static reference => (reference.uri, reference.kind)).Distinct())
        {
            this.StartDemand(uri, kind, targets: null);
        }
    }

    // The demand follows the window's active scene document; another document, or none, retires it.
    private void OnActiveDocumentChanged(object? sender, EventArgs args)
        => this.Dispatch(() =>
        {
            this.InvalidateObsoleteDemands();
            if (this.documentArea.ActiveDocument is { Content: SceneContent } active && this.sceneSync.GetDocumentScene(active.Id) is { } loaded)
            {
                this.Activate(loaded, active);
            }
            else
            {
                this.Reset();
            }
        });

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
        this.Follow(null);
    }

    // An edit of the followed document may drop a reference a demand was for.
    private void Follow(Document? owner)
    {
        if (ReferenceEquals(this.document, owner))
        {
            return;
        }

        if (this.document is { } previous)
        {
            previous.StateChanged -= this.OnDocumentStateChanged;
        }

        this.document = owner;
        if (owner is not null)
        {
            owner.StateChanged += this.OnDocumentStateChanged;
        }
    }

    private void OnDocumentStateChanged(object? sender, EventArgs args) => this.Dispatch(this.InvalidateObsoleteDemands);

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
