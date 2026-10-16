# Milestone 7: replica closure and release readiness (design addendum)

An addendum to `2026-10-13-interceptor-port-design.md`. Its six milestones are merged; this one closes what they
carried and prepares the public release. The Three.js frontend stays a separate project afterwards (section 11 of
the design), against the scene API as the milestone-6 follow-ups document hands it over.

## 1. Goal

Every rule of the original that a capture, the rig or the code can still settle is settled and the port follows it;
what cannot be observed is stated as such in its note and nowhere pinned by a test. The repository is clean-room by
construction and by scan. The browser version keeps the flight log across visits and is checked in three browser
engines. A fresh-history public repository with a hosted page can be produced by one script, and the private
repository keeps its history (the user's ruling).

## 2. Scope: the open items of `docs/superpowers/plans/2026-10-16-milestone-6-followups.md`

Every item there is assigned to one task of the plan and ends in one of three states: CLOSED (the rule in the note,
the port follows it, a test pins it), UNOBSERVABLE (the note states why and what would be needed; the code keeps a
NOTE-GAP and no test pins the choice), or OUT OF SCOPE with the reason (only the Three.js frontend and the items a
physical device alone can check).

## 3. Method

- **Captures first, code second.** Three capture rounds (sound, game flow, combat) run in parallel; each is a note
  author with the emulator (one at a time under the lock) and the rig, producing capture scripts under
  `tools/vamiga-ref/scripts/` (peek, step, key and run-time pokes only; no image bytes), note updates, and a list of
  port deviations. A code round then applies the lists, with the tests and goldens.
- **The rig with the whole state.** For rules that read memory outside the loaded tables (the landmark junk-model and
  beyond-hunk cases), the rig loads the state hunk and its neighbours at the original's addresses from a dump taken
  in flight (`extracted/`, git-ignored), so the port can be compared on the exact bytes the original reads.
- **Clean room by scan.** A tool (`tools/cleanroom/scan.py`) slides a 12-byte window with at least 6 distinct bytes
  over every tracked file and every extracted asset and the disk, and lists every match; counting sequences and the
  extractor's own scaffolding are allowed by an explicit list with the reason per entry. It runs locally (the assets
  are never in CI) and gates the public export.
- **Persistence.** The browser build mounts an IDBFS directory for the flight log; the core's save points call the
  frontend's sync; a denied or full storage falls back to the in-memory file with one message in the page.
- **Public export.** `tools/release/export-public.sh` builds a one-commit repository from `main`'s tree: the ignored
  directories cannot be in it, the local goldens and `.superpowers/` are not tracked, the scan must pass, the public
  README and the licence are added, and a Pages workflow deploys `build/web` (no asset). The script writes the result
  to a directory outside the repository and a git bundle; the user creates the GitHub repository and pushes.

## 4. Non-goals

The Three.js frontend; checks that need a physical controller or a hardware audio output (the checklist document
lists them for the user); any change to the private repository's history.
