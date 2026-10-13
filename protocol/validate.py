#!/usr/bin/env python3
"""validate.py -- executable spec for the ECDI_fake_harness protocol.

The protocol document says what the message stream must look like; this script
is what actually enforces it.  Any fixture in the tree is checked against every
invariant in PROTOCOL.md section 5, and encoding is checked too (a BOM in a
.jsonl breaks streaming parsers, so it is a hard failure rather than a style
note).

Convention, and it is the whole configuration surface:
  * fixtures/hostile/**   MUST violate at least one invariant.  A hostile
                          fixture that triggers nothing is itself a failure --
                          a negative test that passes by doing nothing is
                          indistinguishable from no test at all.
  * everything else       MUST satisfy every invariant.

Exit code = number of fixtures that failed their expectation (0 = all good),
the same convention ecdi_tests and doc_lint.py use.

Output is ASCII-only on purpose: this machine's console code page is GBK, so
printing CJK from a message body to stdout raises UnicodeEncodeError.
"""

import argparse
import io
import json
import os
import sys

BOM = b"\xef\xbb\xbf"

KNOWN_TYPES = {
    "hello", "state", "block_start", "delta", "block_end", "ask", "usage",
    "error", "bye",                       # core -> gui
    "prompt", "cancel", "answer",         # gui -> core
}
CORE_TYPES = {"hello", "state", "block_start", "delta", "block_end", "ask",
              "usage", "error", "bye"}

# PROTOCOL 3.3 block kinds.  Kept here so the validator and the GUI agree on one
# list: Transcript::IsKnownKind in gui/src/Transcript.cpp is the C++ twin, and a
# new kind must be added in both (the note below fires if they drift apart).
KNOWN_KINDS = {"user", "assistant", "tool_call", "tool_result", "system", "error"}

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_FIXTURES = os.path.join(HERE, "fixtures")


def ascii_safe(value):
    return "".join(ch if 32 <= ord(ch) < 127 else "?" for ch in value)


def say(text=""):
    sys.stdout.write(ascii_safe(text) + "\n")


class Findings(object):
    """Collects invariant violations, tagged by which invariant fired."""

    def __init__(self):
        self.violations = []          # (invariant_id, line_no, message)
        self.notes = []

    def hit(self, inv, line, message):
        self.violations.append((inv, line, message))

    def note(self, message):
        self.notes.append(message)

    def ids(self):
        return sorted(set(v[0] for v in self.violations))


def read_fixture(path, findings):
    with open(path, "rb") as handle:
        raw = handle.read()
    if raw.startswith(BOM):
        findings.hit("ENC", 1, "file starts with a UTF-8 BOM")
        raw = raw[3:]
    if b"\r\n" in raw:
        findings.hit("ENC", 0, "file uses CRLF; fixtures must be LF")
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        findings.hit("ENC", 0, "not valid UTF-8: %s" % exc)
        return []
    return text.split("\n")


def parse(lines, findings):
    """Returns [(line_no, message_dict_or_None)]."""
    out = []
    for i, line in enumerate(lines, 1):
        if line == "":
            continue                       # trailing newline artefact
        try:
            msg = json.loads(line)
        except ValueError as exc:
            findings.hit("PARSE", i, "unparsable line: %s" % str(exc)[:70])
            out.append((i, None))
            continue
        if not isinstance(msg, dict) or "t" not in msg:
            findings.hit("PARSE", i, "not an object carrying 't'")
            out.append((i, None))
            continue
        if msg["t"] not in KNOWN_TYPES:
            findings.hit("I5", i, "unknown type %r (must be ignored, not fatal)"
                         % msg["t"])
        out.append((i, msg))
    return out


def check_stream(parsed, findings):
    """Every invariant that can be checked on a core->gui fixture."""
    seqs = [(i, m["seq"]) for i, m in parsed
            if m is not None and "seq" in m]
    if not seqs:
        findings.hit("I4", 0, "no message carries 'seq'")
    else:
        for (li, a), (lj, b) in zip(seqs, seqs[1:]):
            if b != a + 1:
                findings.hit("I4", lj, "seq jumped %d -> %d (loss must be "
                                       "surfaced, never ignored)" % (a, b))
        if seqs[0][1] != 0:
            findings.hit("I4", seqs[0][0], "first seq is %d, must be 0"
                         % seqs[0][1])

    started, ended = {}, {}
    for i, m in parsed:
        if m is None:
            continue
        t = m["t"]
        if t == "block_start":
            block = m.get("id")
            if block is None:
                findings.hit("I1", i, "block_start without 'id'")
            elif block in started:
                findings.hit("I1", i, "duplicate block_start for %r" % block)
            else:
                started[block] = i
        elif t == "block_end":
            block = m.get("id")
            if block not in started:
                findings.hit("I1", i, "block_end for never-started %r" % block)
            elif block in ended:
                findings.hit("I1", i, "duplicate block_end for %r" % block)
            else:
                ended[block] = i

    for block in started:
        if block not in ended:
            findings.hit("I1", started[block],
                         "block %r started but never ended" % block)

    # I2: a delta must sit strictly between its block's start and end.
    for i, m in parsed:
        if m is None or m["t"] != "delta":
            continue
        block = m.get("id")
        if block not in started:
            findings.hit("I2", i, "delta for never-started %r" % block)
        elif block in ended and i > ended[block]:
            findings.hit("I2", i, "delta for %r arrives after its block_end"
                         % block)

    # I7: a tool_result must point at a known tool_call.
    calls = {m.get("id") for _i, m in parsed
             if m is not None and m["t"] == "block_start"
             and m.get("kind") == "tool_call"}
    for i, m in parsed:
        if m is None or m["t"] != "block_start":
            continue
        if m.get("kind") == "tool_result":
            ref = m.get("for")
            if ref not in calls:
                findings.hit("I7", i, "tool_result 'for' does not name a "
                                      "tool_call: %r" % ref)

    # I3, in the only form a fixture can express: right after an 'ask' the core
    # must say state=waiting, and it must not raise a second ask before it has
    # returned to state=running (the answer is implicit -- see PROTOCOL 8.3).
    pending = None
    for i, m in parsed:
        if m is None:
            continue
        if m["t"] == "ask":
            if pending is not None:
                findings.hit("I3", i, "second ask while one is still pending")
            pending = i
        elif m["t"] == "state":
            value = m.get("value")
            if pending is not None and value == "waiting":
                pending = "awaiting"
            elif pending == "awaiting" and value == "running":
                pending = None
            elif pending not in (None, "awaiting") and value not in ("running",):
                findings.hit("I3", i, "only state=waiting may follow an ask "
                                      "(got %r)" % value)
            elif value == "waiting" and pending is None:
                findings.hit("I3", i, "state=waiting with no preceding ask")
    # A fixture may legitimately end while an ask is still pending: the stream
    # simply stops there, and nothing follows, so there is nothing to check.

    # ── unknown block kind: recorded explicitly (2026-10-13) ───────────────
    # WHY this exists: PROTOCOL 3.3 lists exactly six block kinds, and I5 only
    # governs the ENVELOPE discriminant `t` -- so an unknown `kind` is covered by
    # no invariant at all.  That gap was found the hard way: a fixture full of
    # reasoning/thinking blocks validated green while the GUI rendered every one
    # of them as a collapsed card titled with the tool_result placeholder
    # ("等待结果"), making chain-of-thought indistinguishable from a pending tool
    # result.  See .scratch/cot-findings.md for the probe and screenshot.
    # Open-source models emit chain-of-thought, so this path is not hypothetical.
    #
    # A note, not a violation: I5's forward-compatibility spirit says unknown
    # content is ignored-and-recorded, not rejected.  But silence was the actual
    # defect, so this must be visible in the report either way.
    # `m.get("kind") is not None` guards the sort: a missing field yields None,
    # and sorted() would then compare str with NoneType (hit for real on the
    # first run of this check -- the absent-kind fixture crashed the validator).
    unknown_kinds = sorted({
        m.get("kind") for _i, m in parsed
        if m is not None and m["t"] == "block_start"
        and m.get("kind") is not None and m.get("kind") not in KNOWN_KINDS})
    # An absent `kind` is its own case (GetString would yield ""), and it lands in
    # the same neutral rendering path -- so report it rather than hiding it.
    missing_kind = sum(
        1 for _i, m in parsed
        if m is not None and m["t"] == "block_start" and not m.get("kind"))
    if unknown_kinds:
        findings.note("unknown block kind(s) %s -- rendered neutrally, not as "
                      "tool cards (PROTOCOL 3.3 has no such value)"
                      % ", ".join(repr(k) for k in unknown_kinds))
    if missing_kind:
        findings.note("%d block_start without a 'kind' field -- rendered "
                      "neutrally" % missing_kind)

    # shape notes -- informational, never fatal
    first = next((m for _i, m in parsed if m is not None), None)
    last = next((m for _i, m in reversed(parsed) if m is not None), None)
    if first is None or first.get("t") != "hello":
        findings.note("first message is not 'hello'")
    if last is not None and last.get("t") != "bye":
        findings.note("last message is not 'bye'")
    counts = {}
    for _i, m in parsed:
        if m is not None:
            counts[m["t"]] = counts.get(m["t"], 0) + 1
    findings.note("types: %s" % ", ".join("%s=%d" % kv
                                          for kv in sorted(counts.items())))
    findings.note("blocks: %d started, %d ended" % (len(started), len(ended)))


def is_hostile(path, fixtures_root):
    rel = os.path.relpath(path, fixtures_root).replace("\\", "/")
    return rel.split("/")[0] == "hostile"


def collect(fixtures_root):
    found = []
    for base, _dirs, files in os.walk(fixtures_root):
        for name in sorted(files):
            if name.endswith(".jsonl"):
                found.append(os.path.join(base, name))
    return found


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Check every fixture against the protocol invariants.")
    parser.add_argument("--fixtures", default=DEFAULT_FIXTURES,
                        help="fixtures root (default: ./fixtures)")
    parser.add_argument("--quiet", action="store_true",
                        help="only print fixtures that failed")
    opts = parser.parse_args(argv)

    if not os.path.isdir(opts.fixtures):
        say("validate: no fixtures directory at %s" % opts.fixtures)
        return 1

    paths = collect(opts.fixtures)
    if not paths:
        say("validate: no .jsonl fixtures under %s" % opts.fixtures)
        return 1

    failed = 0
    say("validate -- %d fixture(s)" % len(paths))
    for path in paths:
        rel = os.path.relpath(path, opts.fixtures).replace("\\", "/")
        hostile = is_hostile(path, opts.fixtures)
        findings = Findings()
        lines = read_fixture(path, findings)
        parsed = parse(lines, findings)
        check_stream(parsed, findings)
        triggered = findings.ids()

        if hostile:
            ok = bool(triggered)
            verdict = ("OK (triggers %s)" % ",".join(triggered) if ok
                       else "FAIL (hostile fixture triggers nothing)")
        else:
            ok = not triggered
            verdict = "OK" if ok else "FAIL (%s)" % ",".join(triggered)
        if not ok:
            failed += 1
        if opts.quiet and ok:
            continue

        say("")
        say("%-46s %s" % (rel, verdict))
        say("  lines=%d  %s" % (len([l for l in lines if l]),
                                "hostile" if hostile else "must be valid"))
        for note in findings.notes:
            say("  %s" % note)
        for inv, line, message in findings.violations:
            say("  [%s] line %s: %s" % (inv, line, message))

    say("")
    say("%d fixture(s) failed their expectation" % failed
        if failed else "every fixture behaves as advertised")
    return min(failed, 250)


if __name__ == "__main__":
    raise SystemExit(main())
