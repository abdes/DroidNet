// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using DroidNet.Documents;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

/// <summary>
/// A tab whose state the test sets, raising <see cref="PropertyChanged"/> as a document's tab does.
/// </summary>
/// <param name="documentId">
///     The id of the document. If <see langword="null" />, a new id is assigned.
/// </param>
[ExcludeFromCodeCoverage]
public partial class TestDocumentMetadata(DocumentId? documentId = null) : IDocumentMetadata
{
    /// <inheritdoc/>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <inheritdoc/>
    public DocumentId DocumentId { get; } = documentId ?? DocumentId.New();

    /// <inheritdoc/>
    public string Title { get; set => this.Set(ref field, value); } = string.Empty;

    /// <inheritdoc/>
    public Uri? IconUri { get; set => this.Set(ref field, value); }

    /// <inheritdoc/>
    public bool IsDirty { get; set => this.Set(ref field, value); }

    /// <inheritdoc/>
    public bool IsPinnedHint { get; set => this.Set(ref field, value); }

    /// <inheritdoc/>
    public bool IsClosable { get; set => this.Set(ref field, value); } = true;

    private void Set<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (!EqualityComparer<T>.Default.Equals(field, value))
        {
            field = value;
            this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }
}
