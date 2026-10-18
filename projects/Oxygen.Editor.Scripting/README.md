# Oxygen.Editor.Scripting

The script family module: what is true of the editor's scripts whatever shows
them. It holds no view, view model or dialog.

Today it holds the script problems: `ScriptProblem`, one problem the engine's
script analyzer reported or the player raised while a scene played, and its
`ScriptProblemSeverity`. The Content Browser's analysis snapshot and Live
Preview's play session produce them; the Problems panel and the inspector's
script slots show them.

The rest of the script family moves here: the script model and parameters, the
slot rules the inspector and the cook share, the script facts and their ports,
project script tooling, templates and diagnostic codes.
