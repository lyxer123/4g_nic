#!/usr/bin/env python3
"""
Generate unified diffs between official managed components (from idf_component.yml)
and the copies in ./managed_components/<name>/.

Output: patches/managed_components/<name>.patch

Requires: git (uses `git diff --no-index`), network on first run for git clone.

Usage (from repo root):
  python tools/export_managed_component_patches.py
  python tools/export_managed_component_patches.py espressif__iot_bridge
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Files added by the component manager / not in upstream source archives — optional exclude from diff noise
EXCLUDE_NAMES = {".component_hash", "CHECKSUMS.json", ".git", ".gitignore"}


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def parse_idf_component_yml(path: Path) -> dict[str, str] | None:
    text = path.read_text(encoding="utf-8", errors="replace")
    m_repo = re.search(
        r"repository:\s*git://github\.com/([^/\s]+)/([^.#\s]+)(?:\.git)?",
        text,
        re.I,
    )
    if not m_repo:
        m_repo = re.search(
            r"repository:\s*https://github\.com/([^/\s]+)/([^.#\s]+)(?:\.git)?",
            text,
            re.I,
        )
    if not m_repo:
        return None
    owner, repo = m_repo.group(1), m_repo.group(2)
    m_ci = re.search(
        r"repository_info:\s*\n(?:[ \t]+[^\n]+\n)*?\s+commit_sha:\s*([a-f0-9]{7,40})",
        text,
        re.I,
    )
    if not m_ci:
        m_ci = re.search(r"commit_sha:\s*([a-f0-9]{7,40})", text, re.I)
    if not m_ci:
        return None
    commit = m_ci.group(1)
    m_path = re.search(
        r"repository_info:\s*\n(?:[ \t]+[^\n]+\n)*?\s+path:\s*(.+?)\s*$",
        text,
        re.M,
    )
    subpath = m_path.group(1).strip() if m_path else "."
    subpath = subpath.strip('"').strip("'")
    return {"owner": owner, "repo": repo, "commit": commit, "path": subpath}


def copy_tree_filtered(src: Path, dst: Path) -> None:
    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True)

    def ignore(dirpath: str, names: list[str]) -> list[str]:
        out = []
        for n in names:
            if n in EXCLUDE_NAMES:
                out.append(n)
        return out

    shutil.copytree(src, dst, dirs_exist_ok=True, ignore=ignore)


def git_clone_at_commit(owner: str, repo: str, commit: str, dest: Path) -> None:
    url = f"https://github.com/{owner}/{repo}.git"
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        shutil.rmtree(dest)
    subprocess.run(
        ["git", "clone", "--quiet", url, str(dest)],
        check=True,
    )
    subprocess.run(
        ["git", "-C", str(dest), "fetch", "--quiet", "--depth", "1", "origin", commit],
        check=True,
    )
    subprocess.run(
        ["git", "-C", str(dest), "checkout", "--quiet", commit],
        check=True,
    )


def normalize_git_diff_paths(patch_text: str, left_label: str, right_label: str) -> str:
    """
    git diff --no-index (cwd=parent) with dirs 'upstream' and 'local' produces:
      diff --git a/upstream/foo b/local/foo
    Rewrite to:
      diff --git a/foo b/foo
    """
    lp = f"{left_label.rstrip('/')}/"
    rp = f"{right_label.rstrip('/')}/"

    lines = patch_text.splitlines(keepends=True)
    out: list[str] = []
    for line in lines:
        s = line
        if s.startswith("diff --git "):
            m = re.match(r"^diff --git a/(.+?) b/(.+?)\s*$", s.rstrip("\r\n"))
            if m:
                a, b = m.group(1), m.group(2)
                if a.startswith(lp):
                    a = a[len(lp) :]
                if b.startswith(rp):
                    b = b[len(rp) :]
                s = f"diff --git a/{a} b/{b}\n"
        elif s.startswith("--- "):
            s = _strip_hunk_path(s, "--- ", lp, rp, left_label, right_label)
        elif s.startswith("+++ "):
            s = _strip_hunk_path(s, "+++ ", lp, rp, left_label, right_label)
        out.append(s)
    return "".join(out)


def _strip_hunk_path(
    line: str, prefix: str, lp: str, rp: str, left_label: str, right_label: str
) -> str:
    nl = line.endswith("\n")
    core = line.rstrip("\n")
    rest = core[len(prefix) :]
    if rest.startswith('"'):
        return line
    split = rest.split("\t", 1)
    path = split[0].strip()
    tab = "\t" + split[1] if len(split) > 1 else ""
    if path == "/dev/null":
        return line
    newp = _strip_ab_path(path, lp, rp, left_label, right_label)
    out = prefix + newp + tab
    return out + ("\n" if nl else "")


def _strip_ab_path(path: str, lp: str, rp: str, left_label: str, right_label: str) -> str:
    """a/upstream/foo -> a/foo when lp is upstream/."""
    _ = left_label, right_label
    if path.startswith("a/" + lp):
        return "a/" + path[len("a/" + lp) :]
    if path.startswith("b/" + rp):
        return "b/" + path[len("b/" + rp) :]
    return path


def run_diff(left: Path, right: Path, left_name: str = "upstream", right_name: str = "local") -> str:
    r = subprocess.run(
        [
            "git",
            "diff",
            "--no-index",
            "-p",
            "--ignore-cr-at-eol",
            left_name,
            right_name,
        ],
        cwd=str(left.parent),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    # git diff returns 1 when files differ
    raw = r.stdout or ""
    return normalize_git_diff_paths(raw, left_name, right_name)


def export_one(
    root: Path,
    component_name: str,
    out_dir: Path,
    cache_dir: Path,
) -> tuple[str, bool]:
    mc = root / "managed_components" / component_name
    yml = mc / "idf_component.yml"
    if not yml.is_file():
        return (f"skip {component_name}: no idf_component.yml", False)
    meta = parse_idf_component_yml(yml)
    if not meta:
        return (f"skip {component_name}: cannot parse repository/commit", False)

    clone_dir = cache_dir / f"{meta['owner']}_{meta['repo']}_{meta['commit'][:7]}"
    if not clone_dir.is_dir():
        git_clone_at_commit(meta["owner"], meta["repo"], meta["commit"], clone_dir)

    sub = meta["path"].strip().rstrip("/")
    if sub in (".", ""):
        upstream_root = clone_dir
    else:
        upstream_root = clone_dir / sub.replace("/", os.sep)
    if not upstream_root.is_dir():
        return (f"fail {component_name}: upstream path missing {upstream_root}", False)

    work = tempfile.mkdtemp(prefix="mc_diff_")
    try:
        w = Path(work)
        left = w / "upstream"
        right = w / "local"
        copy_tree_filtered(upstream_root, left)
        copy_tree_filtered(mc, right)
        diff_raw = run_diff(left, right)
        if not diff_raw.strip():
            return (f"ok {component_name}: identical to upstream {meta['commit'][:7]}", True)
        out_file = out_dir / f"{component_name}.patch"
        meta_file = out_dir / f"{component_name}.meta.txt"
        rel = out_file.relative_to(root).as_posix()
        meta_file.write_text(
            "\n".join(
                [
                    f"upstream: https://github.com/{meta['owner']}/{meta['repo']}",
                    f"commit: {meta['commit']}",
                    f"subpath: {meta['path']}",
                    "",
                    "Apply to a FRESH copy of this component (e.g. after idf.py update-dependencies),",
                    "from inside the component directory:",
                    f"  cd managed_components/{component_name}",
                    f"  git apply --check ../../../patches/managed_components/{component_name}.patch",
                    f"  git apply ../../../patches/managed_components/{component_name}.patch",
                    "",
                    f"(Patch paths are relative to the component root; repo-relative: {rel})",
                    "",
                ]
            ),
            encoding="utf-8",
            newline="\n",
        )
        out_file.write_text(diff_raw, encoding="utf-8", newline="\n")
        return (f"ok {component_name}: wrote {out_file.name} ({len(diff_raw)} bytes)", True)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "components",
        nargs="*",
        help="managed component dir names (default: all under managed_components/)",
    )
    args = ap.parse_args()
    root = repo_root()
    out_dir = root / "patches" / "managed_components"
    out_dir.mkdir(parents=True, exist_ok=True)
    cache_dir = root / "patches" / ".upstream_cache"
    cache_dir.mkdir(parents=True, exist_ok=True)

    if args.components:
        names = args.components
    else:
        names = sorted(
            p.name
            for p in (root / "managed_components").iterdir()
            if p.is_dir() and (p / "idf_component.yml").is_file()
        )

    ok_n = 0
    fail_n = 0
    for name in names:
        msg, ok = export_one(root, name, out_dir, cache_dir)
        print(msg)
        if ok:
            ok_n += 1
        else:
            fail_n += 1

    index_lines = [
        "# [name.patch] → apply under managed_components/<name>/ (INI style, cf. iot_bridge/patches/patches.list)",
        "# Regenerate: python tools/export_managed_component_patches.py",
        "",
    ]
    for pf in sorted(out_dir.glob("*.patch")):
        comp = pf.stem
        index_lines.append(f"[{pf.name}]")
        index_lines.append(f"    path = managed_components/{comp}")
        index_lines.append("")

    index_file = out_dir / "patches.list"
    index_file.write_text("\n".join(index_lines).rstrip() + "\n", encoding="utf-8")
    print(f"\nWrote {index_file.relative_to(root)}")
    return 0 if fail_n == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
