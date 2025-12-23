// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Importer registration helper.
/// </summary>
public sealed class ImportPluginRegistration
{
    private readonly ImporterRegistry registry;

    /// <summary>
    /// Initializes a new instance of the <see cref="ImportPluginRegistration"/> class.
    /// </summary>
    /// <param name="registry">The importer registry.</param>
    public ImportPluginRegistration(ImporterRegistry registry)
    {
        ArgumentNullException.ThrowIfNull(registry);
        this.registry = registry;
    }

    /// <summary>
    /// Adds an importer.
    /// </summary>
    /// <param name="importer">The importer.</param>
    public void AddImporter(IAssetImporter importer) => this.registry.Register(importer);
}
