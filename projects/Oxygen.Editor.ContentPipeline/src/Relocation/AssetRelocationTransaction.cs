// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json;
using DroidNet.Storage;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// Applies a relocation plan: moves files, then writes rewritten referrers against their baselines. A journal
/// written before the first change lets any failure, or an interrupted process, restore every file.
/// </summary>
internal static partial class AssetRelocationTransaction
{
    private const string JournalFolder = "relocation";
    private const string JournalName = "journal.json";

    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    private enum Phase
    {
        Applying,
        Committed,
        RolledBack,
    }

    /// <summary>Applies a plan, or restores every file and rethrows when any step fails.</summary>
    /// <param name="projectRoot">The project root.</param>
    /// <param name="plan">The plan, prepared against the current files.</param>
    /// <param name="files">The atomic file store.</param>
    /// <param name="logger">Records the journal, each move, each rewrite and any restore.</param>
    /// <param name="failAfterStep">Test hook: fails after this many applied steps.</param>
    /// <param name="cancellationToken">Cancels before the first change.</param>
    /// <returns>Each rewritten file with the version written, after every move and rewrite is committed.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Any failure after the first change must restore the project before it is reported.")]
    public static async Task<IReadOnlyList<RelocatedFile>> ApplyAsync(string projectRoot, AssetRelocationPlan plan, IAtomicFileStore files, ILogger logger, int? failAfterStep, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(plan);
        await RequirePreparedStateAsync(plan, files, cancellationToken).ConfigureAwait(false);
        cancellationToken.ThrowIfCancellationRequested();
        var journal = new Journal
        {
            Phase = Phase.Applying,
            Moves = [.. plan.Moves.Select(static move => new JournalMove(move.Source, move.Target, move.IsDirectory))],
            CreatedDirectories = [.. GetMissingParents(plan.Moves)],
            Edits = [.. plan.Edits.Select(static edit => new JournalEdit(edit.FinalPath, edit.Before.Sha256, Hash(edit.AfterContent), edit.BeforeContent))],
        };
        var path = Path.Combine(projectRoot, ".build", JournalFolder, Guid.NewGuid().ToString("N"), JournalName);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        await WriteJournalAsync(files, path, journal).ConfigureAwait(false);
        LogJournalOpened(logger, path, journal.Moves.Length, journal.Edits.Length);

        // From here on, a failure restores the project; cancellation no longer applies.
        var step = 0;
        var written = new List<RelocatedFile>();
        try
        {
            await ApplyChangesAsync(
                plan,
                journal,
                files,
                logger,
                () =>
                {
                    if (failAfterStep is { } limit && ++step > limit)
                    {
                        throw new IOException("Simulated relocation failure.");
                    }
                },
                written).ConfigureAwait(false);
        }
        catch (Exception failure)
        {
            LogRestoring(logger, failure, path);
            await RestoreAsync(files, path, journal, logger, failure).ConfigureAwait(false);
            throw new AssetRelocationException("The move failed and every file was restored: " + failure.Message, failure);
        }

        journal = journal with { Phase = Phase.Committed };
        await WriteJournalAsync(files, path, journal).ConfigureAwait(false);
        DeleteJournal(path, logger);
        LogCommitted(logger, path);
        return written;
    }

    /// <summary>Restores every relocation a process interruption left half-applied.</summary>
    /// <param name="projectRoot">The project root.</param>
    /// <param name="files">The atomic file store.</param>
    /// <param name="logger">Records each recovered journal and restored file.</param>
    /// <param name="cancellationToken">Cancels before the next journal.</param>
    /// <returns>Completion after each interrupted relocation is restored.</returns>
    public static async Task RecoverAsync(string projectRoot, IAtomicFileStore files, ILogger logger, CancellationToken cancellationToken)
    {
        var root = Path.Combine(projectRoot, ".build", JournalFolder);
        if (!Directory.Exists(root))
        {
            return;
        }

        foreach (var path in Directory.EnumerateFiles(root, JournalName, SearchOption.AllDirectories).ToArray())
        {
            cancellationToken.ThrowIfCancellationRequested();
            Journal? journal;
            try
            {
                journal = JsonSerializer.Deserialize<Journal>(await File.ReadAllBytesAsync(path, cancellationToken).ConfigureAwait(false), JsonOptions);
            }
            catch (JsonException error)
            {
                // The journal is written atomically, so an unreadable one never recorded a started relocation.
                LogUnreadableJournal(logger, error, path);
                DeleteJournal(path, logger);
                continue;
            }

            if (journal?.Phase == Phase.Applying)
            {
                LogRecovering(logger, path);
                await RestoreAsync(files, path, journal, logger, failure: null).ConfigureAwait(false);
            }
            else
            {
                DeleteJournal(path, logger);
            }
        }
    }

    private static async Task RequirePreparedStateAsync(AssetRelocationPlan plan, IAtomicFileStore files, CancellationToken cancellationToken)
    {
        foreach (var edit in plan.Edits)
        {
            var current = await files.ReadAsync(edit.OriginalPath, cancellationToken).ConfigureAwait(false);
            if (current.Version != edit.Before)
            {
                throw new AssetRelocationException($"'{Path.GetFileName(edit.OriginalPath)}' changed while the move was prepared. Try again.");
            }
        }

        foreach (var move in plan.Moves)
        {
            var exists = move.IsDirectory ? Directory.Exists(move.Source) : File.Exists(move.Source);
            if (!exists || (!string.Equals(move.Source, move.Target, StringComparison.OrdinalIgnoreCase) && (File.Exists(move.Target) || Directory.Exists(move.Target))))
            {
                throw new AssetRelocationException($"'{Path.GetFileName(move.Source)}' or its destination changed while the move was prepared. Try again.");
            }
        }
    }

    private static async Task ApplyChangesAsync(
        AssetRelocationPlan plan,
        Journal journal,
        IAtomicFileStore files,
        ILogger logger,
        Action step,
        List<RelocatedFile> written)
    {
        foreach (var directory in journal.CreatedDirectories)
        {
            _ = Directory.CreateDirectory(directory);
        }

        foreach (var move in journal.Moves)
        {
            step();
            Move(move.Source, move.Target, move.IsDirectory);
            LogMoved(logger, move.Source, move.Target);
        }

        foreach (var edit in plan.Edits)
        {
            step();
            written.Add(new(edit.OriginalPath, edit.FinalPath, await files.WriteAsync(edit.FinalPath, edit.AfterContent, edit.Before, CancellationToken.None).ConfigureAwait(false)));
            LogRewrote(logger, edit.FinalPath);
        }
    }

    private static async Task RestoreAsync(IAtomicFileStore files, string path, Journal journal, ILogger logger, Exception? failure)
    {
        var errors = new List<Exception>();
        await RestoreEditsAsync(files, journal, logger, errors).ConfigureAwait(false);
        RestoreMoves(journal, logger, errors);
        RemoveCreatedDirectories(journal, errors);
        if (errors.Count != 0)
        {
            foreach (var error in errors)
            {
                LogRestoreFailed(logger, error, path);
            }

            // Keep the journal so the next project activation retries the restore.
            throw new AssetRelocationException(
                "The move failed and some files could not be restored. They are restored again when the project opens; inspect the project before editing it.",
                new AggregateException([.. failure is null ? [] : new[] { failure }, .. errors]));
        }

        await WriteJournalAsync(files, path, journal with { Phase = Phase.RolledBack }).ConfigureAwait(false);
        DeleteJournal(path, logger);
        LogRestored(logger, path);
    }

    private static async Task RestoreEditsAsync(IAtomicFileStore files, Journal journal, ILogger logger, List<Exception> errors)
    {
        foreach (var edit in journal.Edits.AsEnumerable().Reverse())
        {
            try
            {
                var current = await files.ReadAsync(edit.Path, CancellationToken.None).ConfigureAwait(false);
                if (current.Version.Exists && string.Equals(current.Version.Sha256, edit.AfterSha256, StringComparison.OrdinalIgnoreCase))
                {
                    _ = await files.WriteAsync(edit.Path, edit.Before, current.Version, CancellationToken.None).ConfigureAwait(false);
                    LogRestoredFile(logger, edit.Path);
                }
                else if (!string.Equals(current.Version.Sha256, edit.BeforeSha256, StringComparison.OrdinalIgnoreCase) && current.Version.Exists)
                {
                    errors.Add(new StorageWriteConflictException($"'{edit.Path}' changed after the failed move; it was left as is."));
                }
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException or StorageException)
            {
                errors.Add(error);
            }
        }
    }

    private static void RestoreMoves(Journal journal, ILogger logger, List<Exception> errors)
    {
        foreach (var move in journal.Moves.AsEnumerable().Reverse())
        {
            try
            {
                var targetExists = move.IsDirectory ? Directory.Exists(move.Target) : File.Exists(move.Target);
                var sourceExists = move.IsDirectory ? Directory.Exists(move.Source) : File.Exists(move.Source);
                var caseOnly = string.Equals(move.Source, move.Target, StringComparison.OrdinalIgnoreCase);
                if (caseOnly ? targetExists && !HasExactName(move.Source) : targetExists && !sourceExists)
                {
                    Move(move.Target, move.Source, move.IsDirectory);
                    LogMovedBack(logger, move.Target, move.Source);
                }
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException)
            {
                errors.Add(error);
            }
        }
    }

    private static void RemoveCreatedDirectories(Journal journal, List<Exception> errors)
    {
        foreach (var directory in journal.CreatedDirectories.AsEnumerable().Reverse())
        {
            try
            {
                if (Directory.Exists(directory) && !Directory.EnumerateFileSystemEntries(directory).Any())
                {
                    Directory.Delete(directory);
                }
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException)
            {
                errors.Add(error);
            }
        }
    }

    private static void Move(string source, string target, bool isDirectory)
    {
        if (string.Equals(source, target, StringComparison.OrdinalIgnoreCase))
        {
            var temporary = Path.Combine(Path.GetDirectoryName(source)!, ".oxygen-relocate-" + Guid.NewGuid().ToString("N"));
            Move(source, temporary, isDirectory);
            Move(temporary, target, isDirectory);
            return;
        }

        if (isDirectory)
        {
            Directory.Move(source, target);
        }
        else
        {
            File.Move(source, target);
        }
    }

    private static bool HasExactName(string path)
    {
        var folder = Path.GetDirectoryName(path)!;
        return Directory.Exists(folder) && Directory.EnumerateFileSystemEntries(folder, Path.GetFileName(path))
            .Any(entry => string.Equals(Path.GetFileName(entry), Path.GetFileName(path), StringComparison.Ordinal));
    }

    private static List<string> GetMissingParents(IEnumerable<RelocationFileMove> moves)
    {
        var created = new List<string>();
        foreach (var move in moves)
        {
            var missing = new Stack<string>();
            for (var folder = Path.GetDirectoryName(move.Target); folder is not null && !Directory.Exists(folder); folder = Path.GetDirectoryName(folder))
            {
                missing.Push(folder);
            }

            created.AddRange(missing.Where(folder => !created.Contains(folder, StringComparer.OrdinalIgnoreCase)));
        }

        return created;
    }

    private static string Hash(byte[] content) => Convert.ToHexString(SHA256.HashData(content));

    private static async Task WriteJournalAsync(IAtomicFileStore files, string path, Journal journal)
    {
        var current = await files.ReadAsync(path, CancellationToken.None).ConfigureAwait(false);
        _ = await files.WriteAsync(path, JsonSerializer.SerializeToUtf8Bytes(journal, JsonOptions), current.Version, CancellationToken.None).ConfigureAwait(false);
    }

    private static void DeleteJournal(string path, ILogger logger)
    {
        try
        {
            Directory.Delete(Path.GetDirectoryName(path)!, recursive: true);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            // A settled journal left behind is ignored by recovery and removed next time.
            LogJournalNotDeleted(logger, error, path);
        }
    }

    private sealed record Journal
    {
        public int Version { get; init; } = 1;

        public Phase Phase { get; init; }

        public JournalMove[] Moves { get; init; } = [];

        public string[] CreatedDirectories { get; init; } = [];

        public JournalEdit[] Edits { get; init; } = [];
    }

    private sealed record JournalMove(string Source, string Target, bool IsDirectory);

    private sealed record JournalEdit(string Path, string BeforeSha256, string AfterSha256, byte[] Before);
}
