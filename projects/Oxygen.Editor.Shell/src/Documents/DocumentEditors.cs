// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The editor of each document kind the project session opens: one descriptor per kind of <see cref="OpenDocuments"/>.
/// </summary>
public sealed class DocumentEditors
{
    private readonly Dictionary<IDocumentKind, DocumentEditorDescriptor> byKind;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentEditors"/> class.
    /// </summary>
    /// <param name="documents">The open documents of the session, whose kinds need an editor each.</param>
    /// <param name="editors">The editor descriptors, one for each kind of <paramref name="documents"/>.</param>
    /// <exception cref="InvalidOperationException">
    ///     A kind of <paramref name="documents"/> has no descriptor or two, or a descriptor names a kind that
    ///     <paramref name="documents"/> does not open.
    /// </exception>
    public DocumentEditors(OpenDocuments documents, IEnumerable<DocumentEditorDescriptor> editors)
    {
        ArgumentNullException.ThrowIfNull(documents);
        ArgumentNullException.ThrowIfNull(editors);
        this.byKind = new Dictionary<IDocumentKind, DocumentEditorDescriptor>(ReferenceEqualityComparer.Instance);
        foreach (var editor in editors)
        {
            if (!documents.Kinds.Contains(editor.Kind))
            {
                throw new InvalidOperationException($"The {editor.Kind.Name} editor names a kind that no document opens with.");
            }

            if (!this.byKind.TryAdd(editor.Kind, editor))
            {
                throw new InvalidOperationException($"The {editor.Kind.Name} kind has two editors.");
            }
        }

        if (documents.Kinds.FirstOrDefault(kind => !this.byKind.ContainsKey(kind)) is { } missing)
        {
            throw new InvalidOperationException($"The {missing.Name} kind has no editor.");
        }
    }

    /// <summary>Gets the editor of a kind the session opens.</summary>
    /// <param name="kind">The kind.</param>
    /// <returns>The kind's editor descriptor.</returns>
    public DocumentEditorDescriptor Of(IDocumentKind kind) => this.byKind[kind];
}
