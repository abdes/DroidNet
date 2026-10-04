// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.RegularExpressions;
using DroidNet.Controls.Demo.Tree.Services;

namespace DroidNet.Controls.Demo.Tree.Model;

/// <summary>
/// The root <see cref="DynamicTree" /> item adapter for the currently opened <see cref="Scene" />.
/// </summary>
/// <param name="scene">The opened scene represented by the root tree item.</param>
internal sealed partial class SceneAdapter(Scene scene) : TreeItemAdapter(isRoot: true, isHidden: false), ITreeItem<Scene>
{
    /// <summary>
    /// A regular expression pattern to validate a suggested scene name. It checks
    /// that the name can be used for a file name.
    /// </summary>
    /// <see href="https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file?redirectedfrom=MSDN" />
    private const string ValidNamePattern
        = """^(?!^(PRN|AUX|CLOCK\$|NUL|CON|COM\d|LPT\d|\.\.|\.)(\.|\..|[^ ]*\.{1,2})?$)([^<>:"/\\|?*\x00-\x1F]+[^<>:"/\\|?*\x00-\x1F\ .])$""";

    private string label = scene.Name;

    /// <inheritdoc/>
    public override string Label
    {
        get => this.label;
        set
        {
            if (string.Equals(value, this.label, StringComparison.Ordinal))
            {
                return;
            }

            this.label = value;
            this.OnPropertyChanged();
        }
    }

    /// <inheritdoc/>
    public Scene AttachedObject => scene;

    /// <inheritdoc/>
    public override bool ValidateItemName(string name) => ValidNameMatcher().IsMatch(name);

    /// <inheritdoc/>
    protected override int DoGetChildrenCount() => this.AttachedObject.Entities.Count;

    /// <inheritdoc/>
    protected override async Task LoadChildren()
    {
        await SceneLoaderService.LoadSceneAsync(this.AttachedObject).ConfigureAwait(false);

        foreach (var entity in this.AttachedObject.Entities)
        {
            this.AddChildInternal(
                new EntityAdapter(entity)
                {
                    IsExpanded = false,
                });
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    [GeneratedRegex(ValidNamePattern, RegexOptions.ExplicitCapture, matchTimeoutMilliseconds: 1000)]
    private static partial Regex ValidNameMatcher();
}
