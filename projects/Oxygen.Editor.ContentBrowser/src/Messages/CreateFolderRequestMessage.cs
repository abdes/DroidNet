// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>Asks the sources tree to create a folder in a writable folder and start naming it in place.</summary>
/// <param name="ParentFolder">The folder that receives the new folder.</param>
public sealed record CreateFolderRequestMessage(AssetPath ParentFolder);
