// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Shell.Contributions;

/// <summary>
/// The editor that a module contributes for its document kind. A window's document area shows each document with the
/// editor its kind's provider describes.
/// </summary>
public interface IDocumentEditorProvider
{
    /// <summary>Gets the editor of the provider's document kind.</summary>
    public DocumentEditorDescriptor Editor { get; }
}
