#!/usr/bin/env python3
"""
Sync the RTA project board with the ILO pages.

  1. Creates the tasks the ILOs mention that don't exist on GitHub yet
     (currently: "Diffraction around corners"), as a sub-issue of its parent,
     on the project board and closed as done.
  2. Labels the reconstructed sub-tasks (#43-#50 and the new ones) "reconstructed".
  3. Replaces every "[Task: X] <!-- TODO: link the issue once it exists ... -->"
     placeholder in the ILO pages with a link, and fixes links to issues that
     don't exist (ILO 1 points at #51 for the diffraction task).
  4. Prints a report: sub-issue structure to verify, task names in the ILOs that
     don't match an issue, and issues on GitHub that no ILO mentions.

It is a dry run by default: it only reads from GitHub and prints what it would
do. Pass --apply to make the changes.

Requirements
  - Python 3.8+
  - GitHub CLI (https://cli.github.com), logged in with the project scope:
        gh auth login
        gh auth refresh -s project

Usage
  python rta_board_sync.py                      # dry run
  python rta_board_sync.py --apply              # do it
  python rta_board_sync.py --apply --diffraction-hours 3
  python rta_board_sync.py --ilo-dir path/to/ilos
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = "TygovGorp/RTA"
PROJECT_OWNER = "TygovGorp"
PROJECT_NUMBER = "1"
ASSIGNEE = "TygovGorp"
ISSUE_URL = f"https://github.com/{REPO}/issues/"

LABEL = "reconstructed"
LABEL_COLOR = "C5DEF5"
LABEL_DESCRIPTION = "Split off afterwards; hours reconstructed from commits, not tracked at the time"

# Sub-tasks that were split off afterwards and already exist on the board.
RECONSTRUCTED = [43, 44, 45, 46, 47, 48, 49, 50]

ILO_FILES = ["ILO1.md", "ILO2.md", "ILO3.md"]

# Tasks the ILOs need that don't exist on GitHub yet.
MISSING_TASKS = [
    {
        "title": "Diffraction around corners",
        "parent": 25,                # Spatialization
        "milestone": "Ceiling",
        "status": "Done",
        "close": True,
        "hours_arg": "diffraction_hours",
        # Issue number the ILOs already use for it, if any. Replaced with the real number.
        "placeholder_number": 51,
        "body": """**What:** Sound that bends around a corner comes from the corner and is filtered per band, the way diffraction makes it lose more high than low frequencies. The route search behind the arrival direction already finds the corner a sound bends around; diffraction adds the knife-edge loss from ITU-R P.526-16 for the extra path length at that corner.

> **Reconstructed:** this task was split off afterwards from the spatialization work. The work was done on 13 Oct (`d3813f5`, `f683d35`); the hours are reconstructed from those commits, not tracked at the time.

**Acceptance Criteria:**
- [x] When the direct path is blocked, the arrival route search returns the corner the sound bends around, and the log shows it (`Corner=1`, with the extra path length as `Excess=` in cm)
- [x] The diffraction gain is calculated per band with the knife-edge model from ITU-R P.526-16 (eq. 31), with ν = 2·√(Δ/λ) from the extra path length Δ
- [x] High frequencies lose more than low ones, and a longer detour gives a lower gain in every band (`Diff=[...]` in the log)
- [x] It works for walls that let sound through and for walls that don't
- [x] It can be switched off with `rta.Diffraction 0` for an A/B comparison
""",
    },
]

# Parents whose sub-issues are printed, so the structure can be checked by eye.
VERIFY_PARENTS = {18: "Reverb", 25: "Spatialization", 32: "Reverb polish", 36: "Apparent source direction"}

PLACEHOLDER_RE = re.compile(
    r"🟣 \[Task: (?P<title>[^\]]+)\] <!-- TODO: link the issue once it exists[^>]*-->"
)
TASK_MENTION_RE = re.compile(r"\[Task: (?P<title>[^\]]+)\]")


# --------------------------------------------------------------------------- gh helpers

class GhError(RuntimeError):
    pass


def gh(args, stdin=None):
    """Run gh and return stdout. Raises GhError with gh's own message on failure."""
    try:
        result = subprocess.run(
            ["gh", *args], input=stdin, capture_output=True, text=True, encoding="utf-8"
        )
    except FileNotFoundError:
        sys.exit("GitHub CLI (gh) not found. Install it from https://cli.github.com and run `gh auth login`.")
    if result.returncode != 0:
        raise GhError(f"gh {' '.join(args)}\n{result.stderr.strip()}")
    return result.stdout


def gh_json(args):
    out = gh(args)
    return json.loads(out) if out.strip() else None


class Board:
    """Every write goes through here, so a dry run prints instead of acting."""

    def __init__(self, apply):
        self.apply = apply
        self.issues = {}            # number -> {"title", "url", "labels"}
        self._project = None

    def write(self, description, args, stdin=None):
        if not self.apply:
            print(f"  [dry run] {description}")
            return None
        print(f"  {description}")
        return gh(args, stdin=stdin)

    # -- reading
    def load_issues(self):
        data = gh_json(["issue", "list", "-R", REPO, "--state", "all", "--limit", "500",
                        "--json", "number,title,url,labels"])
        self.issues = {
            i["number"]: {"title": i["title"], "url": i["url"],
                          "labels": {l["name"] for l in i.get("labels", [])}}
            for i in data
        }

    def find_by_title(self, title):
        wanted = normalise(title)
        for number, issue in self.issues.items():
            if normalise(issue["title"]) == wanted:
                return number
        return None

    def sub_issues(self, number):
        data = gh_json(["api", f"repos/{REPO}/issues/{number}/sub_issues", "--paginate"]) or []
        return [(i["number"], i["title"]) for i in data]

    def project(self):
        if self._project is None:
            view = gh_json(["project", "view", PROJECT_NUMBER, "--owner", PROJECT_OWNER, "--format", "json"])
            fields = gh_json(["project", "field-list", PROJECT_NUMBER, "--owner", PROJECT_OWNER,
                              "--format", "json", "--limit", "100"])
            self._project = {"id": view["id"], "fields": {f["name"]: f for f in fields["fields"]}}
        return self._project

    # -- writing
    def ensure_label(self):
        self.write(f"create or update label '{LABEL}'",
                   ["label", "create", LABEL, "-R", REPO, "--color", LABEL_COLOR,
                    "--description", LABEL_DESCRIPTION, "--force"])

    def add_label(self, number):
        if LABEL in self.issues.get(number, {}).get("labels", set()):
            print(f"  #{number} already has '{LABEL}'")
            return
        self.write(f"label #{number} '{LABEL}'",
                   ["issue", "edit", str(number), "-R", REPO, "--add-label", LABEL])

    def create_issue(self, task):
        out = self.write(
            f"create issue '{task['title']}' (milestone {task['milestone']}, label '{LABEL}')",
            ["issue", "create", "-R", REPO, "--title", task["title"], "--body-file", "-",
             "--label", LABEL, "--milestone", task["milestone"], "--assignee", ASSIGNEE],
            stdin=task["body"])
        if out is None:
            return None
        match = re.search(r"/issues/(\d+)", out)
        if not match:
            raise GhError(f"Could not read the new issue number from: {out!r}")
        number = int(match.group(1))
        self.issues[number] = {"title": task["title"], "url": ISSUE_URL + str(number), "labels": {LABEL}}
        print(f"    -> #{number}")
        return number

    def add_sub_issue(self, parent, child):
        if child is None:
            self.write(f"add the new issue as sub-issue of #{parent}", [])
            return
        if child in [n for n, _ in self.sub_issues(parent)]:
            print(f"  #{child} is already a sub-issue of #{parent}")
            return
        child_id = gh_json(["api", f"repos/{REPO}/issues/{child}"])["id"]
        self.write(f"add #{child} as sub-issue of #{parent}",
                   ["api", "--method", "POST", f"repos/{REPO}/issues/{parent}/sub_issues",
                    "-F", f"sub_issue_id={child_id}"])

    def add_to_project(self, number, status=None, hours=None):
        if number is None:
            self.write(f"add the new issue to project {PROJECT_NUMBER}"
                       + (f", Status={status}" if status else "")
                       + (f", Time Taken={hours}" if hours is not None else ""), [])
            return
        out = self.write(f"add #{number} to project {PROJECT_NUMBER}",
                         ["project", "item-add", PROJECT_NUMBER, "--owner", PROJECT_OWNER,
                          "--url", ISSUE_URL + str(number), "--format", "json"])
        if out is None:
            return
        item_id = json.loads(out)["id"]
        project = self.project()

        if status:
            field = project["fields"].get("Status")
            option = next((o for o in (field or {}).get("options", []) if o["name"] == status), None)
            if field and option:
                self.write(f"set #{number} Status={status}",
                           ["project", "item-edit", "--id", item_id, "--project-id", project["id"],
                            "--field-id", field["id"], "--single-select-option-id", option["id"]])
            else:
                print(f"  ! no Status option '{status}' on the board; set it by hand")

        if hours is not None:
            field = project["fields"].get("Time Taken")
            if field:
                self.write(f"set #{number} Time Taken={hours}",
                           ["project", "item-edit", "--id", item_id, "--project-id", project["id"],
                            "--field-id", field["id"], "--number", str(hours)])
            else:
                print("  ! no 'Time Taken' field on the board; set it by hand")

    def close(self, number):
        if number is None:
            self.write("close the new issue as completed", [])
            return
        self.write(f"close #{number} as completed",
                   ["issue", "close", str(number), "-R", REPO, "--reason", "completed"])


def normalise(title):
    """Case- and punctuation-insensitive title match ('Shoebox Validation' == 'Shoebox validation')."""
    return re.sub(r"[^a-z0-9]+", " ", title.lower()).strip()


# --------------------------------------------------------------------------- ILO files

def read_text(path):
    raw = path.read_bytes().decode("utf-8")
    return raw.replace("\r\n", "\n"), ("\r\n" in raw)


def write_text(path, text, crlf):
    if crlf:
        text = text.replace("\n", "\r\n")
    path.write_bytes(text.encode("utf-8"))


def patch_ilos(board, ilo_dir, renumber, hours_by_title, apply):
    """Fill link placeholders and renumber links to issues that don't exist."""
    for name in ILO_FILES:
        path = ilo_dir / name
        if not path.exists():
            print(f"  ! {path} not found, skipped")
            continue
        text, crlf = read_text(path)
        original = text
        changes = []

        def fill(match):
            title = match.group("title")
            number = board.find_by_title(title)
            if number is None:
                return match.group(0)                         # still missing: leave the TODO
            changes.append(f"link '{title}' -> #{number}")
            return f"🟣 [Task: {title}]({ISSUE_URL}{number})"

        text = PLACEHOLDER_RE.sub(fill, text)

        for old, new in renumber.items():
            pattern = re.compile(rf"\[#{old}\]\({re.escape(ISSUE_URL)}{old}\)")
            text, count = pattern.subn(f"[#{new}]({ISSUE_URL}{new})", text)
            if count:
                changes.append(f"#{old} -> #{new} ({count}x)")

        # Fill the tracked hours of a newly created task in the ILO 3 task table.
        for title, hours in hours_by_title.items():
            number = board.find_by_title(title)
            if number is None:
                continue
            row = re.compile(rf"(\| 🟣 \[Task: {re.escape(title)}\]\({re.escape(ISSUE_URL)}{number}\) \|[^|\n]*\|[^|\n]*\|) – \|")
            text, count = row.subn(rf"\1 {hours:g} h |", text)
            if count:
                changes.append(f"'{title}' time tracked -> {hours:g} h")

        if text == original:
            print(f"  {name}: no changes")
            continue
        for change in changes:
            print(f"  {name}: {'' if apply else '[dry run] '}{change}")
        if apply:
            write_text(path, text, crlf)


def report(board, ilo_dir):
    print("\nSub-issue structure (check that this matches the ILO tables):")
    for parent, label in VERIFY_PARENTS.items():
        try:
            subs = board.sub_issues(parent)
        except GhError as error:
            print(f"  #{parent} {label}: could not read ({error})")
            continue
        listed = ", ".join(f"#{n}" for n, _ in subs) or "none"
        print(f"  #{parent} {label}: {listed}")

    mentioned_titles, mentioned_numbers = set(), set()
    for name in ILO_FILES:
        path = ilo_dir / name
        if path.exists():
            text, _ = read_text(path)
            mentioned_titles |= {m.group("title") for m in TASK_MENTION_RE.finditer(text)}
            mentioned_numbers |= {int(n) for n in re.findall(rf"{re.escape(ISSUE_URL)}(\d+)", text)}

    unknown = sorted(t for t in mentioned_titles if board.find_by_title(t) is None)
    print("\nTask names in the ILOs with no issue of that exact title (fine if they're shorthand):")
    print("  " + ("\n  ".join(unknown) if unknown else "none"))

    dangling = sorted(n for n in mentioned_numbers if n not in board.issues)
    print("\nIssue links in the ILOs that point at issues that don't exist:")
    print("  " + (", ".join(f"#{n}" for n in dangling) if dangling else "none"))

    unmentioned = sorted(n for n in board.issues if n not in mentioned_numbers)
    print("\nIssues on GitHub that no ILO links to:")
    print("  " + ("\n  ".join(f"#{n} {board.issues[n]['title']}" for n in unmentioned) if unmentioned else "none"))


# --------------------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--apply", action="store_true", help="make the changes (default: dry run)")
    parser.add_argument("--ilo-dir", default=".", help="folder with ILO1.md, ILO2.md and ILO3.md")
    parser.add_argument("--diffraction-hours", type=float, default=None,
                        help="hours spent on diffraction; sets Time Taken on the board and in ILO 3")
    args = parser.parse_args()
    ilo_dir = Path(args.ilo_dir)

    print("DRY RUN: nothing is changed. Run with --apply to make these changes.\n" if not args.apply
          else "APPLYING CHANGES\n")

    board = Board(args.apply)
    try:
        board.load_issues()

        print(f"Label '{LABEL}':")
        board.ensure_label()

        print("\nMissing tasks:")
        renumber, hours_by_title = {}, {}
        created = []
        for task in MISSING_TASKS:
            hours = getattr(args, task["hours_arg"]) if task.get("hours_arg") else None
            existing = board.find_by_title(task["title"])
            if existing is not None:
                # Already created (by hand or an earlier run): make sure it's labelled and linked.
                print(f"  '{task['title']}' already exists as #{existing}")
                number = existing
                board.add_label(number)
                board.add_sub_issue(task["parent"], number)
            else:
                # The label is set at creation, so no separate add_label here.
                number = board.create_issue(task)
                created.append(number)
                board.add_sub_issue(task["parent"], number)
                board.add_to_project(number, task.get("status"), hours)
                if task.get("close"):
                    board.close(number)

            old = task.get("placeholder_number")
            if old and old not in board.issues:
                renumber[old] = number if number is not None else "<new>"
            if hours is not None:
                hours_by_title[task["title"]] = hours

        print(f"\nReconstructed sub-tasks get the '{LABEL}' label:")
        for number in RECONSTRUCTED:
            if number in board.issues:
                board.add_label(number)
            else:
                print(f"  ! #{number} not found on GitHub")

        print("\nILO pages:")
        if any(v == "<new>" for v in renumber.values()):
            print("  (dry run: the new issue's number is only known after --apply, shown as <new>)")
        patch_ilos(board, ilo_dir, {k: v for k, v in renumber.items()}, hours_by_title, args.apply)

        report(board, ilo_dir)
    except GhError as error:
        sys.exit(f"\ngh failed:\n{error}\n\nIf this is about permissions, run: gh auth refresh -s project")

    if not args.apply:
        print("\nDry run finished. Run again with --apply to make these changes.")


if __name__ == "__main__":
    main()
