# Prompt: Update XML Documentation Comments in C# Files (Indentation-locked)

Goal
Ensure XML documentation comments (`/// ...`) are accurate and complete while preserving EXACT whitespace inside XML tags. Any whitespace deviation inside tags is a hard failure.

---

1. Scope
- Existing XMLDoc comments
- New XMLDoc comments to be added for non-documented non-private members

---

2. Indentation Lock (hard constraints)
- Inside multi-line XML elements (`<summary>`, `<remarks>`, `<param>`, `<typeparam>`, `<returns>`, `<exception>`):
  - Lines that are not opening or closing tags MUST start with `///    ` (four spaces after `///`).
- CDATA blocks must be byte-for-byte identical (no changes inside `<![CDATA[ ... ]]>`).

Regex enforcement (apply pre- and post-edit checks):
- Multi-line inner line: `^///    \S.*$`
- Opening tag: `^/// <([a-z]+)(\s[^>]*)?>\s*$`
- Closing tag: `^/// </[a-z]+>\s*$`
- Tags inside <remarks> or <summary>:
  - Opening tag: `^///    <para>\s*$`
  - Closing tag: `^///    </para>\s*$`
- `code` open: `^///    <code><!\[CDATA\[$`
- `code` close: `^///    \]\]></code>$`

If any post-edit line inside an element fails these regexs, revert the change or mark as FAIL.

---

3. Formatting Rules
- Use `<see langword="null"/>`, `<see langword="true"/>`, `<see langword="false"/>` for keywords.
- `<summary>` and `<remarks>`:
  - First paragraph: single line without `<para>`.
  - Subsequent paragraphs: wrap in `<para>` and use EXACT 4-space indentation for inner lines.
- Single-line elements (<120 chars): keep on one line, no reflow.
- Multi-line elements: opening and closing tags on their own lines; inner lines start with EXACT 4 spaces.

---

4. <param>, <typeparam>, <returns>, <exception>
- Always include `name` for `<param>`/`<typeparam>`; match symbol exactly.
- Use `<paramref name="..."/>` when referencing parameters.
- Add `<exception>` only when the code clearly throws (e.g., `ArgumentNullException.ThrowIfNull`).
- Line-length rule: if >120 chars, convert to multi-line, enforcing the indentation lock.

---

5. Prohibited edits (hard failures)
- Changing any leading whitespace after `///` on inner lines of multi-line elements.
- Altering CDATA content or its indentation.
- Expanding single-line elements that are under 120 chars.
- Modifying any non-`///` lines.

---

6. Post-edit validation (must perform)
- Re-scan file and validate with the regexs in section 2.
- Run diff: confirm only `///` lines changed.
- If build or XML-doc lint is available, report result; otherwise state “not run.”
- If validation fails: list offending lines and revert or mark as FAIL with reasons.

---

7. Output format
- One-line summary per file.
- List of TODOs inserted for uncertain behavior.
- Validation status:
  - "Regex indentation: PASS/FAIL"
  - "Diff scope: PASS/FAIL"
  - "Build/XML-doc lint: PASS/NOT RUN"

---

8. Examples (strictly preserved indentation)
Short param (single line):
/// <param name="watch">Whether to watch the file for changes.</param>

Long param (multi-line):
/// <param name="encryption">
///     Optional type of encryption provider. Must implement <see cref="IEncryptionProvider"/>.
/// </param>

Exception (single line):
/// <exception cref="System.ArgumentNullException">Thrown when <paramref name="container"/> is <see langword="null"/>.</exception>

Exception (multi-line):
/// <exception cref="System.InvalidOperationException">
///     Thrown when the configuration source cannot be initialized (e.g., invalid JSON or missing dependencies).
/// </exception>

CDATA (unchanged):
/// <code><![CDATA[
///     var bootstrapper = new Bootstrapper(args)
///         .Configure()
///         .WithConfig()
///         .WithJsonConfigSource(...)
///         .Build();
/// ]]></code>
