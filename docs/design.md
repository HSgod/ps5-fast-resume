# Design notes

## Dump lifecycle

- A dump is created only when switching from game A to game B. Launching B while A runs makes ShellCore close A; that close path is where the dump is taken (A is suspended until the dump is written, then the close continues).
- A dump is deleted when the game is closed for real: user picks "Close game", the game exits or crashes on its own, or the dump fails to restore.
- One dump per title id (`/data/fast-resume/<TITLE>.frd`); a new dump of the same title replaces the old one.

## Detecting game launch

- On every app launch, check whether `/data/fast-resume/<TITLE>.frd` exists for its title id (PPSA/CUSA). If yes, show a "Quick Resume" notification and restore.
- No need to inspect every process: only app processes have a title id, system processes are `NPXS*`. Options:
  - Hook the app launch path in ShellCore (same place as the close hook), which gives the title id directly.
  - Fallback: poll the process list (one sysctl, ~30 entries) a few times per second and only look at new pids with a non-NPXS title id.
- Restore runs after the game has started normally and its files are mounted (see restore preconditions), not at process creation.

## Distinguishing close reasons

The ShellCore close hook must tell apart:
- close because another game is starting -> dump, keep
- explicit user close / app exit / crash -> delete dump
