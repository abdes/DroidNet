// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>Asks the sources tree to create a folder in a writable folder and start naming it in place.</summary>
/// <param name="ParentFolder">The virtual path of the folder that receives the new folder.</param>
public sealed record CreateFolderRequestMessage(string ParentFolder);
