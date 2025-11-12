from .get_artifacts import run
import sys


def main():
    # Reuse existing CLI parsing and logic in run()
    return run()


if __name__ == "__main__":
    sys.exit(main())
