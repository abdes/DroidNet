# Update C# XML documentation

Update existing `///` comments and document missing non-private contracts in the
requested files. Read the implementation first; follow `.editorconfig` and the
file's established XMLDoc indentation, not a whole-file reflow.

- Change only documentation comments; preserve implementation and unrelated user
  edits. Keep existing indentation after `///` and source indentation before it.
  For new comments, match adjacent documentation. Preserve CDATA byte-for-byte
  unless changing the example is explicitly requested.
- Keep short single-line elements intact. Wrap new/changed long prose at the
  local limit (120 columns where the existing convention uses it); do not combine
  contradictory whitespace locks with forced reformatting of existing comments.
- Use valid XML, matching `<param name="...">`/`<typeparam name="...">`,
  `<paramref>`, `<see cref="...">` and `<see langword="...">` references.
  Describe non-void results and actual exceptions; do not invent guarantees.
- In summaries/remarks, keep the first paragraph unwrapped and use `<para>` for
  subsequent paragraphs where consistent with the file. Document ownership,
  lifecycle and failure behavior only when supported by the code.
- Check edited XML, symbol references, indentation and diff scope. Regexes alone
  cannot validate nested XML or indented source comments. Fix your own mistakes
  without reverting unrelated edits; leave uncertain claims out and report them.
- Report changed files, unresolved contract questions and checks actually run.
  Build/XMLDoc checks must be reported as not run when unavailable or out of scope.
