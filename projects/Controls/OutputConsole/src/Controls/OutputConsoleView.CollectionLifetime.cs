// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Reactive;
using System.Reactive.Linq;
using System.Reactive.Subjects;

namespace DroidNet.Controls.OutputConsole;

/// <summary>Keeps buffered output scoped to the displayed source and the current loaded lifetime.</summary>
public sealed partial class OutputConsoleView
{
    private void StartCollectionProcessing()
    {
        this.StopCollectionProcessing();
        var changes = new Subject<NotifyCollectionChangedEventArgs>();
        var resets = new Subject<Unit>();
        this.collectionChanges = changes;
        this.resetRequests = resets;
        this.collectionChangesSubscription = changes.Buffer(TimeSpan.FromMilliseconds(16))
            .Where(static batch => batch.Count > 0)
            .Subscribe(batch => _ = this.DispatcherQueue.TryEnqueue(() =>
            {
                if (this.isLoaded && ReferenceEquals(this.collectionChanges, changes))
                {
                    foreach (var change in batch)
                    {
                        this.ProcessCollectionChange(change);
                    }
                }
            }));
        this.resetSubscription = resets.Sample(TimeSpan.FromMilliseconds(100))
            .Subscribe(_ => this.DispatcherQueue.TryEnqueue(() =>
            {
                if (this.isLoaded && ReferenceEquals(this.resetRequests, resets))
                {
                    this.RebuildView();
                }
            }));
    }

    private void StopCollectionProcessing()
    {
        this.collectionChangesSubscription?.Dispose();
        this.resetSubscription?.Dispose();
        this.collectionChanges?.Dispose();
        this.resetRequests?.Dispose();
        this.collectionChangesSubscription = null;
        this.resetSubscription = null;
        this.collectionChanges = null;
        this.resetRequests = null;
    }
}
