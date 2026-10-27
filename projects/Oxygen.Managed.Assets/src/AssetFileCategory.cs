// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets;

/// <summary>What a file of a known type is, in the asset pipeline.</summary>
public enum AssetFileCategory
{
    /// <summary>A cooked descriptor the engine loads, such as <c>.omat</c>.</summary>
    Cooked,

    /// <summary>A cooked resource table or data file, such as <c>textures.table</c>.</summary>
    CookedResource,

    /// <summary>An authored JSON descriptor that cooks into the descriptor of the same name, such as <c>.omat.json</c>.</summary>
    Descriptor,

    /// <summary>A script source that cooks into a script descriptor.</summary>
    Script,

    /// <summary>An input source that cooks into input actions and mapping contexts.</summary>
    Input,

    /// <summary>A model in a foreign format, imported through its import settings.</summary>
    Model,

    /// <summary>An image that textures are imported from.</summary>
    Image,

    /// <summary>The import settings stored beside a model.</summary>
    ImportSettings,

    /// <summary>
    /// How a model is looked at in the Model Editor, beside the model: its preview's environment and sun, in the
    /// editor's scene format. It stands for no asset and never cooks.
    /// </summary>
    ModelPreview,
}
