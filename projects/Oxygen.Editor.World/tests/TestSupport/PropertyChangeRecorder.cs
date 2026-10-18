// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;

namespace Oxygen.Editor.World.Tests.TestSupport;

/// <summary>
/// Records property changing and property changed notifications for assertions.
/// </summary>
[ExcludeFromCodeCoverage]
internal sealed class PropertyChangeRecorder
{
    private readonly List<string?> changing = [];
    private readonly List<string?> changed = [];

    public PropertyChangeRecorder(INotifyPropertyChanged item)
    {
        ArgumentNullException.ThrowIfNull(item);
        item.PropertyChanged += (_, args) => this.changed.Add(args.PropertyName);

        if (item is INotifyPropertyChanging changingItem)
        {
            changingItem.PropertyChanging += (_, args) => this.changing.Add(args.PropertyName);
        }
    }

    public IReadOnlyList<string?> Changing => this.changing;

    public IReadOnlyList<string?> Changed => this.changed;

    public int ChangingCount(string propertyName)
        => this.changing.Count(p => string.Equals(p, propertyName, StringComparison.Ordinal));

    public int ChangedCount(string propertyName)
        => this.changed.Count(p => string.Equals(p, propertyName, StringComparison.Ordinal));

    public void Reset()
    {
        this.changing.Clear();
        this.changed.Clear();
    }
}
