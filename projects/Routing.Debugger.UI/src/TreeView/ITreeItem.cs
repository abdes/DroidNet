// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.TreeView;

public interface ITreeItem
{
    bool IsExpanded { get; set; }

    bool IsRoot { get; }

    IEnumerable<ITreeItem> Children { get; }

    string Label { get; }

    bool HasChildren { get; }
}

public interface ITreeItem<out T> : ITreeItem
{
    public T Item { get; }

    public new IEnumerable<ITreeItem<T>> Children { get; }
}
