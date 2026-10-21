// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     Gives the editors of the documents shown in one window that window's services, and takes them back when their
///     tabs leave it.
/// </summary>
/// <remarks>
///     The editors belong to the project session; the window lends them its document area, commands, placement and asset
///     reveal while it shows their tabs (<see cref="IDocumentWindowEditor"/>).
/// </remarks>
public sealed class DocumentWindowAttachments : IDisposable
{
    private readonly DocumentArea area;
    private readonly DocumentService documents;
    private readonly DocumentWindowServices services;
    private bool disposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentWindowAttachments"/> class.
    /// </summary>
    /// <param name="area">The window's document area.</param>
    /// <param name="documents">The session's document service, which creates the editors.</param>
    /// <param name="commands">The window's commands.</param>
    /// <param name="placement">Where the window shows documents and panels.</param>
    /// <param name="reveal">Reveals assets in the window, if the window has a part that does.</param>
    public DocumentWindowAttachments(
        DocumentArea area,
        DocumentService documents,
        ICommandService commands,
        IPlacementService placement,
        IAssetReveal? reveal = null)
    {
        ArgumentNullException.ThrowIfNull(area);
        ArgumentNullException.ThrowIfNull(documents);
        this.area = area;
        this.documents = documents;
        this.services = new DocumentWindowServices(area.Window, area, commands, placement, reveal);

        this.documents.EditorCreated += this.OnEditorCreated;
        this.area.TabRemoved += this.OnTabRemoved;
        foreach (var document in this.area.Documents)
        {
            (this.documents.EditorOf(document) as IDocumentWindowEditor)?.AttachWindow(this.services);
        }
    }

    /// <summary>
    ///     Takes the window's services back from the editors of the documents it still shows.
    /// </summary>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.documents.EditorCreated -= this.OnEditorCreated;
        this.area.TabRemoved -= this.OnTabRemoved;
        foreach (var document in this.area.Documents)
        {
            (this.documents.EditorOf(document) as IDocumentWindowEditor)?.DetachWindow();
        }
    }

    private void OnEditorCreated(object? sender, DocumentTabEventArgs args)
    {
        if (this.area.TabOf(args.Tab.Document) is not null)
        {
            (this.documents.EditorOf(args.Tab.Document) as IDocumentWindowEditor)?.AttachWindow(this.services);
        }
    }

    private void OnTabRemoved(object? sender, DocumentTabEventArgs args)
        => (this.documents.EditorOf(args.Tab.Document) as IDocumentWindowEditor)?.DetachWindow();
}
