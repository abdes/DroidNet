// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.WorldEditor.Editors;

/// <summary>
/// Represents a document shown in the TabbedDocumentView.
/// </summary>
public partial class TabbedDocumentItem : ObservableObject
{
    public TabbedDocumentItem(string title, Func<UIElement> createContent)
    {
        this.Title = title;
        this.CreateContent = createContent;
    }

    public Func<UIElement> CreateContent { get; }

    [ObservableProperty]
    public partial string Title { get; set; }

    [ObservableProperty]
    public partial bool IsClosable { get; internal set; } = true;

    [ObservableProperty]
    public partial bool IsPinned { get; set; } = false;
}
