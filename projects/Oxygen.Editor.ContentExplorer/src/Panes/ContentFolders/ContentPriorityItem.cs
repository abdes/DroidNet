// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>Displays one source in the priority dialog.</summary>
/// <param name="Source">The persisted source reference.</param>
/// <param name="Name">The concise source label.</param>
/// <param name="Path">Its physical root.</param>
public sealed record ContentPriorityItem(CookedContentSource Source, string Name, string Path);
