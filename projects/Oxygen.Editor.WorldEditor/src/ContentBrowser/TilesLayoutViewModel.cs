// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

public class TilesLayoutViewModel(AssetsIndexingService assetsIndexingService)
{
    public ObservableCollection<GameAsset> Assets { get; } = assetsIndexingService.Assets;
}
