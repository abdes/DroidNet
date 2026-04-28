// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Stable content-pipeline operation-kind names used by editor operation results.
/// </summary>
public static class ContentPipelineOperationKinds
{
    /// <summary>
    /// Engine descriptor generation.
    /// </summary>
    public const string DescriptorGenerate = "Content.Descriptor.Generate";

    /// <summary>
    /// Import manifest generation.
    /// </summary>
    public const string ManifestGenerate = "Content.Manifest.Generate";

    /// <summary>
    /// Content import execution.
    /// </summary>
    public const string Import = "Content.Import";

    /// <summary>
    /// Single asset cook.
    /// </summary>
    public const string CookAsset = "Content.Cook.Asset";

    /// <summary>
    /// Current scene cook.
    /// </summary>
    public const string CookScene = "Content.Cook.Scene";

    /// <summary>
    /// Folder cook.
    /// </summary>
    public const string CookFolder = "Content.Cook.Folder";

    /// <summary>
    /// Full project cook.
    /// </summary>
    public const string CookProject = "Content.Cook.Project";

    /// <summary>
    /// Cooked output inspection.
    /// </summary>
    public const string CookedOutputInspect = "Content.CookedOutput.Inspect";

    /// <summary>
    /// Cooked output validation.
    /// </summary>
    public const string CookedOutputValidate = "Content.CookedOutput.Validate";

    /// <summary>
    /// Post-cook catalog refresh.
    /// </summary>
    public const string CatalogRefresh = "Content.Catalog.Refresh";
}
