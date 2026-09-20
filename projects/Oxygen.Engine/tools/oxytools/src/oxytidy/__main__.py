"""Allow the installed package to run with python -m oxytidy."""

from .cli import main

raise SystemExit(main())
