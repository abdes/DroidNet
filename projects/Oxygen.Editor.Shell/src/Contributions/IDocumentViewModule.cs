// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;

namespace Oxygen.Editor.Shell.Contributions;

/// <summary>
///     Registers the views of a feature's document editors in every window that shows the feature's documents: the
///     workspace window and the document windows that tabs are torn out into.
/// </summary>
/// <remarks>
///     A document's tab can move between windows, and each window locates the view of the tab's editor in its own scope.
///     The module registers views only: a document window has no panels, commands or services of the feature.
/// </remarks>
public interface IDocumentViewModule : IScopeModule<IWindowLayer>;
