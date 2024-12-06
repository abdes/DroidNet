// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// The ViewModel for the tiles layout view.
/// </summary>
/// <param name="assetsIndexingService">The service responsible for indexing assets.</param>
public class TilesLayoutViewModel(AssetsIndexingService assetsIndexingService)
{
    /// <summary>
    /// Gets the collection of game assets.
    /// </summary>
    public ObservableCollection<GameAsset> Assets { get; } = assetsIndexingService.Assets;
}
