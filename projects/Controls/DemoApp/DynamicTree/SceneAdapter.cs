// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using System.Text.RegularExpressions;
using DroidNet.Controls.Demo.Model;
using DroidNet.Controls.Demo.Services;

/// <summary>
/// A <see cref="DynamicTree" /> item adapter for the <see cref="Scene" /> model class.
/// </summary>
/// <param name="scene">The <see cref="Entity" /> object to wrap as a <see cref="ITreeItem" />.</param>
public partial class SceneAdapter(Scene scene) : TreeItemAdapter(isRoot: false, isHidden: false), ITreeItem<Scene>
{
    /// <summary>
    /// A regular expression pattern to validate a suggested scene name. It checks
    /// that the name can be used for a file name.
    /// </summary>
    /// <see href="https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file?redirectedfrom=MSDN" />
    private const string ValidNamePattern
        = """^(?!^(PRN|AUX|CLOCK\$|NUL|CON|COM\d|LPT\d|\.\.|\.)(\.|\..|[^ ]*\.{1,2})?$)([^<>:"/\\|?*\x00-\x1F]+[^<>:"/\\|?*\x00-\x1F\ .])$""";

    private string label = scene.Name;

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

    public Scene AttachedObject => scene;

    public override bool ValidateItemName(string name) => ValidNameMatcher().IsMatch(name);

    protected override int GetChildrenCount() => this.AttachedObject.Entities.Count;

    protected override async Task LoadChildren()
    {
        await ProjectLoaderService.LoadSceneAsync(this.AttachedObject).ConfigureAwait(false);

        foreach (var entity in this.AttachedObject.Entities)
        {
            this.AddChildInternal(
                new EntityAdapter(entity)
                {
                    IsExpanded = false,
                });
        }

        await Task.CompletedTask.ConfigureAwait(true);
    }

    [GeneratedRegex(ValidNamePattern, RegexOptions.ExplicitCapture, matchTimeoutMilliseconds: 1000)]
    private static partial Regex ValidNameMatcher();
}
