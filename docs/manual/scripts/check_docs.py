#!/usr/bin/env python3
"""Validate ClassicMac documentation links, images, Mermaid syntax, privacy, and schema coverage."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote

REPO = Path(__file__).resolve().parents[3]
MANUAL_DIR = REPO / "docs/manual"
VMCONFIG_PATH = REPO / "app/Sources/ClassicMac/VMConfig.swift"
NATIVE_MACHINE_PATH = REPO / "native/src/machine.cpp"

ALLOWED_MERMAID_HEADERS = (
    "flowchart TD",
    "flowchart LR",
    "flowchart TB",
    "flowchart RL",
    "graph TD",
    "graph LR",
    "sequenceDiagram",
    "stateDiagram-v2",
    "stateDiagram",
    "classDiagram",
    "erDiagram",
    "xychart-beta",
)

FORBIDDEN_LEAKS = [
    "/Users/" + "austinmcchord",
    "C:\\Users\\" + "Austin",
    "C:/Users/" + "Austin",
]

MD_LINK_RE = re.compile(r"(!?)\[([^\]]*)\]\(([^)]+)\)")
HTML_IMG_RE = re.compile(r"<img\b([^>]*?)>", re.IGNORECASE | re.DOTALL)
HTML_ATTR_SRC_RE = re.compile(r'\bsrc\s*=\s*"([^"]+)"', re.IGNORECASE)
HTML_ATTR_WIDTH_RE = re.compile(r'\bwidth\s*=\s*"?(\d+)"?', re.IGNORECASE)
HEADING_RE = re.compile(r"^(#{1,6})\s+(.+?)\s*$", re.MULTILINE)
FENCE_START_RE = re.compile(r"^```([A-Za-z0-9_-]*)\s*$")


def github_slug(heading_text: str) -> str:
    text = heading_text.strip().lower()
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)
    out = []
    for ch in text:
        if ch.isalnum() or ch in (" ", "-"):
            out.append("-" if ch == " " else ch)
    slug = "".join(out)
    while "--" in slug:
        slug = slug.replace("--", "-")
    return slug.strip("-")


def collect_headings(path: Path) -> set[str]:
    text = path.read_text(encoding="utf-8", errors="replace")
    stripped = strip_fenced_blocks(text)
    slugs: set[str] = set()
    counts: dict[str, int] = {}
    for _, title in HEADING_RE.findall(stripped):
        base = github_slug(title)
        if not base:
            continue
        if base in counts:
            counts[base] += 1
            slugs.add(f"{base}-{counts[base]}")
        else:
            counts[base] = 0
            slugs.add(base)
    return slugs


def strip_fenced_blocks(text: str) -> str:
    out_lines: list[str] = []
    in_fence = False
    for line in text.splitlines():
        if line.strip().startswith("```"):
            in_fence = not in_fence
            continue
        if not in_fence:
            out_lines.append(line)
    return "\n".join(out_lines)


def extract_mermaid_blocks(text: str) -> list[tuple[int, str]]:
    blocks: list[tuple[int, str]] = []
    lines = text.splitlines()
    in_mermaid = False
    start_line = 0
    buf: list[str] = []
    for idx, line in enumerate(lines, start=1):
        stripped = line.strip()
        if not in_mermaid:
            m = FENCE_START_RE.match(stripped)
            if m and m.group(1).lower() == "mermaid":
                in_mermaid = True
                start_line = idx
                buf = []
        else:
            if stripped == "```":
                blocks.append((start_line, "\n".join(buf)))
                in_mermaid = False
            else:
                buf.append(line)
    if in_mermaid:
        blocks.append((start_line, "\n".join(buf)))
    return blocks


def check_mermaid_block(path: Path, start_line: int, block: str) -> list[str]:
    errors: list[str] = []
    lines = [ln for ln in block.splitlines() if ln.strip() and not ln.strip().startswith("%%")]
    if not lines:
        return [f"{path}:{start_line}: Empty Mermaid block"]
    header = lines[0].strip()
    if not any(header.startswith(h) for h in ALLOWED_MERMAID_HEADERS):
        errors.append(f"{path}:{start_line}: Unsupported Mermaid diagram header {header!r}")

    if re.search(r"<(br|b|i|code|small|strong|em|sub|sup)\b[^>]*>", block, re.IGNORECASE):
        errors.append(f"{path}:{start_line}: HTML tag found inside Mermaid block")

    # Check unquoted special characters in flowchart node labels like id[Label (info)]
    if header.startswith(("flowchart", "graph")):
        unquoted_bracket_re = re.compile(r'(?<![A-Za-z0-9_])([A-Za-z0-9_-]+)\[([^"\[\]\n][^\[\]\n]*)\]')
        for offset, raw_line in enumerate(block.splitlines(), start=1):
            s = raw_line.strip()
            if not s or s.startswith(("%%", "flowchart", "graph", "subgraph", "end", "style", "classDef", "linkStyle")):
                continue
            for m in unquoted_bracket_re.finditer(s):
                label = m.group(2)
                if any(c in label for c in ("(", ")", ":", "/", "&", ",")):
                    errors.append(
                        f"{path}:{start_line + offset}: Unquoted special characters in Mermaid node [{label!r}]; wrap in quotes"
                    )
    return errors


def extract_vmconfig_keys() -> list[str]:
    text = VMCONFIG_PATH.read_text(encoding="utf-8")
    m = re.search(r"enum CodingKeys:\s*String,\s*CodingKey\s*\{(.*?)\}", text, re.DOTALL)
    if not m:
        raise RuntimeError(f"Could not find CodingKeys enum in {VMCONFIG_PATH}")
    body = m.group(1)
    # Match `case foo, bar, baz` or `case foo = "raw"`
    keys: list[str] = []
    for line in body.splitlines():
        line = line.split("//", 1)[0].strip()
        if not line.startswith("case "):
            continue
        items = line[len("case "):].split(",")
        for item in items:
            item = item.strip()
            if not item:
                continue
            if "=" in item:
                raw = item.split("=", 1)[1].strip().strip('"')
                keys.append(raw)
            else:
                keys.append(item)
    native_text = NATIVE_MACHINE_PATH.read_text(encoding="utf-8")
    if "swapAltCommand" in native_text and "swapAltCommand" not in keys:
        keys.append("swapAltCommand")
    return keys


def main() -> int:
    errors: list[str] = []
    md_files: list[Path] = []
    for candidate in (REPO / "README.md", REPO / "AGENTS.md", REPO / "CLAUDE.md"):
        if candidate.exists():
            md_files.append(candidate)
    md_files.extend(sorted(MANUAL_DIR.glob("*.md")))

    heading_cache: dict[Path, set[str]] = {}
    for md in md_files:
        heading_cache[md] = collect_headings(md)

    for md in md_files:
        raw = md.read_text(encoding="utf-8")
        rel = md.relative_to(REPO)

        # 1. Privacy leaks
        for leak in FORBIDDEN_LEAKS:
            if leak in raw:
                errors.append(f"{rel}: Forbidden local path leak {leak!r}")

        # 2. Markdown links and images (outside code fences)
        prose = strip_fenced_blocks(raw)
        # Also remove inline code spans so template examples like `[link](foo)` aren't checked falsely
        prose_no_code = re.sub(r"`[^`\n]+`", "", prose)

        for is_img, _alt, target in MD_LINK_RE.findall(prose_no_code):
            target = target.strip()
            if not target or target.startswith(("http://", "https://", "mailto:", "file://")):
                continue
            if target.startswith("#"):
                slug = target[1:]
                if slug not in heading_cache[md]:
                    errors.append(f"{rel}: Broken internal anchor #{slug}")
                continue
            path_part, _, frag = target.partition("#")
            path_part = unquote(path_part)
            resolved = (md.parent / path_part).resolve()
            if not resolved.exists():
                errors.append(f"{rel}: Broken {'image' if is_img else 'link'} target {target!r}")
            elif frag and resolved.suffix.lower() == ".md":
                target_slugs = heading_cache.get(resolved)
                if target_slugs is None:
                    target_slugs = collect_headings(resolved)
                    heading_cache[resolved] = target_slugs
                if frag not in target_slugs:
                    errors.append(f"{rel}: Broken anchor #{frag} in {resolved.relative_to(REPO)}")

        # 3. HTML <img> tags
        for img_attrs in HTML_IMG_RE.findall(prose_no_code):
            src_m = HTML_ATTR_SRC_RE.search(img_attrs)
            if not src_m:
                errors.append(f"{rel}: <img> missing src attribute")
            else:
                src = unquote(src_m.group(1).strip())
                if not src.startswith(("http://", "https://")):
                    resolved = (md.parent / src).resolve()
                    if not resolved.exists():
                        errors.append(f"{rel}: Broken <img> src={src!r}")
            w_m = HTML_ATTR_WIDTH_RE.search(img_attrs)
            if w_m and int(w_m.group(1)) > 840:
                errors.append(f"{rel}: <img> width={w_m.group(1)} exceeds 840px cap")

        # 4. Mermaid validation
        for start_line, block in extract_mermaid_blocks(raw):
            errors.extend(check_mermaid_block(Path(rel), start_line, block))

    # 5. Manual completeness checks
    expected_chapters = [
        "01-system-requirements.md",
        "02-architecture-how-it-works.md",
        "03-installation-and-first-run.md",
        "04-mac-os-library-and-editions.md",
        "05-installing-your-own-mac-os.md",
        "06-everyday-use-and-input.md",
        "07-display-3d-graphics-and-sound.md",
        "08-media-tools-and-sharing.md",
        "09-networking-and-virtual-printing.md",
        "10-creating-and-serving-images.md",
        "11-checkpoints-updates-and-troubleshooting.md",
        "12-configuration-and-cli-reference.md",
    ]
    for chap in expected_chapters:
        if not (MANUAL_DIR / chap).exists():
            errors.append(f"Missing expected manual chapter: docs/manual/{chap}")

    # 6. VMConfig CodingKeys + native keys coverage in Chapter 12
    ch12 = MANUAL_DIR / "12-configuration-and-cli-reference.md"
    if ch12.exists():
        ch12_text = ch12.read_text(encoding="utf-8")
        for key in extract_vmconfig_keys():
            if f"`{key}`" not in ch12_text and key not in ch12_text:
                errors.append(f"docs/manual/12-configuration-and-cli-reference.md: Missing config.json key `{key}`")

    if errors:
        print(f"FAILED: {len(errors)} documentation validation error(s):", file=sys.stderr)
        for err in errors:
            print(f"  - {err}", file=sys.stderr)
        return 1

    print(f"OK: Validated {len(md_files)} Markdown files cleanly (0 errors).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
