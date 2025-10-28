// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.WorldEditor.Editors;

/// <summary>
/// The ViewModel for the <see cref="Oxygen.Editor.WorldEditor.Views.TabbedDocumentView"/> view.
/// Implements a simple document host with windowing support.
/// </summary>
public partial class DocumentHostViewModel : INotifyPropertyChanged
{
    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "{ClassName} constructed")]
    private static partial void LogConstructed(ILogger logger, string className);

    [Conditional("DEBUG")]
    private void LogConstructed()
        => LogConstructed(this.logger, nameof(DocumentHostViewModel));

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Document '{Title}' added at position {Index}")]
    private static partial void LogDocumentAdded(ILogger logger, string title, int index);

    [Conditional("DEBUG")]
    private void LogDocumentAdded(TabbedDocumentItem doc)
        => LogDocumentAdded(this.logger, doc.Title, this.GetTabbedDocumentIndex(doc));

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Cannot close document '{Title}': IsClosable={IsClosable}")]
    private static partial void LogCannotClose(ILogger logger, string title, bool isClosable);

    [Conditional("DEBUG")]
    private void LogCannotClose(TabbedDocumentItem doc)
        => LogCannotClose(this.logger, doc.Title, doc.IsClosable);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Tabbed document closed: '{Title}'")]
    private static partial void LogTabbedDocumentClosed(ILogger logger, string title);

    [Conditional("DEBUG")]
    private void LogTabbedDocumentClosed(TabbedDocumentItem doc)
        => LogTabbedDocumentClosed(this.logger, doc.Title);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Cannot detach document '{Title}': IsClosable={IsClosable}, IsPinned={IsPinned}")]
    private static partial void LogCannotDetach(ILogger logger, string title, bool isClosable, bool isPinned);

    [Conditional("DEBUG")]
    private void LogCannotDetach(TabbedDocumentItem doc)
        => LogCannotDetach(this.logger, doc.Title, doc.IsClosable, doc.IsPinned);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Tabbed document '{Title}' detached")]
    private static partial void LogTabbedDocumentDetached(ILogger logger, string title);

    [Conditional("DEBUG")]
    private void LogTabbedDocumentDetached(TabbedDocumentItem doc)
        => LogTabbedDocumentDetached(this.logger, doc.Title);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Tabbed document '{Title}' is already attached at position {Index}")]
    private static partial void LogDocumentAlreadyAttached(ILogger logger, string title, int index);

    [Conditional("DEBUG")]
    private void LogDocumentAlreadyAttached(TabbedDocumentItem doc)
        => LogDocumentAlreadyAttached(this.logger, doc.Title, this.GetTabbedDocumentIndex(doc));

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Tabbed document '{Title}' attached at position {Index}")]
    private static partial void LogDocumentAttached(ILogger logger, string title, int index);

    [Conditional("DEBUG")]
    private void LogDocumentAttached(TabbedDocumentItem doc)
        => LogDocumentAttached(this.logger, doc.Title, this.GetTabbedDocumentIndex(doc));

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Selection changed from '{From}' at position {FromIndex}, to '{To}' at position {ToIndex}")]
    private static partial void LogSelectionChanged(ILogger logger, string from, int fromIndex, string to, int toIndex);

    [Conditional("DEBUG")]
    private void LogSelectionChanged(int oldIndex, int newIndex)
    {
        var from = oldIndex >= 0 && oldIndex < this.TabbedDocuments.Count
            ? this.TabbedDocuments[oldIndex]
            : null;
        var to = newIndex >= 0 && newIndex < this.TabbedDocuments.Count
            ? this.TabbedDocuments[newIndex]
            : null;
        LogSelectionChanged(this.logger, from?.Title ?? "null", oldIndex, to?.Title ?? "null", newIndex);
    }

    private int GetTabbedDocumentIndex(TabbedDocumentItem? doc)
        => doc is null ? -1 : this.TabbedDocuments.IndexOf(doc);
}
