// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.Collections.ObjectModel;
using System.Collections.Specialized;

internal sealed class SelectionObservableCollection<T> : ObservableCollection<T>
{
    private bool suppressNotification;

    public IDisposable SuspendNotifications() => new NotificationSuspender(this);

    protected override void OnCollectionChanged(NotifyCollectionChangedEventArgs e)
    {
        if (!this.suppressNotification)
        {
            base.OnCollectionChanged(e);
        }
    }

    private sealed class NotificationSuspender : IDisposable
    {
        private readonly SelectionObservableCollection<T> collection;

        public NotificationSuspender(SelectionObservableCollection<T> collection)
        {
            this.collection = collection;
            collection.suppressNotification = true;
        }

        public void Dispose()
        {
            this.collection.suppressNotification = false;
            this.collection.OnCollectionChanged(
                new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
        }
    }
}
