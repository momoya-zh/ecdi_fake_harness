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
    "prompt", "cancel", "answer", "workspace", # gui -> core; workspace also core -> gui ack
    "model",                              # v4: gui -> core request; also core -> gui ack
    "agent",                              # v5: core -> gui subagent lifecycle (control plane)
    "job",                                # v6: core -> gui background job lifecycle (control plane)
    "mcp",                                # v6: core -> gui MCP status update (§3.14, 2026-11-07)
}
CORE_TYPES = {"hello", "state", "block_start", "delta", "block_end", "ask",
              "usage", "error", "bye", "agent", "job", "mcp"}

# PROTOCOL 3.3 block kinds.  Kept here so the validator and the GUI agree on one
# list: Transcript::IsKnownKind in gui/src/Transcript.cpp is the C++ twin, and a
# new kind must be added in both (the note below fires if they drift apart).
#
# v2 (2026-10-14) added "reasoning": chain-of-thought from open-source reasoning
# models.  It is a FIRST-CLASS kind, not a member of the unknown-kind path --
# I8 governs UNKNOWN values only, and reasoning has its own rendering form
# (PROTOCOL 7: collapsed by default, click to expand).  Keeping the two apart is
# the whole point: folding reasoning into the unknown path would make the
# "unknown kinds render neutrally" rule meaningless.
KNOWN_KINDS = {"user", "assistant", "reasoning", "tool_call", "tool_result",
               "system", "error"}

# PROTOCOL 9: the GUI supports up to this protocol major version and REFUSES a
# higher one (no downgrade guessing).  v2 (2026-10-14) added `reasoning` + I8;
# v5 (2026-11-04) added the `agent` message + I12/I13.
# Fixtures declaring <= this are fine; anything above is a hard failure here,
# because such a stream is one the GUI would refuse to connect to at all.
SUPPORTED_PROTOCOL_MAX = 6

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
    # ★ 2026-11-07：末字节必须是 LF（§1.1 分帧 / §1.2 EOF 截断显式报错）。
    #   此前只查 BOM 与 CRLF，末字节漏检——两份「必须有效」的正向夹具
    #   （jobs/background-task、mcp/status）因此长期缺尾 LF：GUI 侧 ReplaySource
    #   每轮都把它们判成「最后一行被截断」并放一条错误通告（§1.2 的报错面是
    #   对的，错的是这些文件本身）。hostile/no-trailing-lf.jsonl 是这条规则的
    #   正向（故意违规）样本。
    if text and not text.endswith("\n"):
        findings.hit("ENC", 0, "final byte is not LF; file is EOF-truncated "
                               "(protocol 1.1 framing / 1.2 explicit error)")
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


def check_stream(parsed, findings, is_resume=False, last_seq=None):
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
        if is_resume:
            if last_seq is not None and seqs[0][1] != last_seq + 1:
                findings.hit("I4", seqs[0][0], "resumed first seq is %d, expected lastSeq+1 (%d)"
                             % (seqs[0][1], last_seq + 1))
        else:
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

    # ---- PROTOCOL 3 (v3, 2026-10-20): I9 user client_id, I10 workspace ack ----
    # I9: every kind:"user" block_start must carry a non-empty client_id, and
    # client_ids must be unique within one stream (the GUI confirms its local
    # bubble by this id -- a duplicate would make the bubble ambiguous).
    # I10: every workspace response must carry request_id / boolean ok / path;
    # ok:false must explain itself with message; request_ids must be unique
    # (only one pending workspace request is allowed at a time, PROTOCOL 3.9).
    # Both apply only to streams declaring protocol >= 3; pre-v3 fixtures are
    # exempt (they predate client_id / workspace entirely).
    declared_protocol = None
    if first is not None and first.get("t") == "hello":
        declared_protocol = first.get("protocol")
    v3_stream = isinstance(declared_protocol, int) and declared_protocol >= 3
    if v3_stream:
        seen_client_ids = set()
        for i, m in parsed:
            if m is None or m["t"] != "block_start" or m.get("kind") != "user":
                continue
            cid = m.get("client_id")
            if not isinstance(cid, str) or not cid:
                findings.hit("I9", i,
                             "kind:\"user\" block_start without a non-empty "
                             "'client_id'")
            elif cid in seen_client_ids:
                findings.hit("I9", i,
                             "duplicate client_id %r across user blocks" % cid)
            else:
                seen_client_ids.add(cid)

        seen_request_ids = set()
        for i, m in parsed:
            if m is None or m["t"] != "workspace":
                continue
            rid = m.get("request_id")
            if not isinstance(rid, str) or not rid:
                findings.hit("I10", i,
                         "workspace response without a non-empty "
                         "'request_id'")
                rid = None
            if rid is not None and rid in seen_request_ids:
                findings.hit("I10", i,
                             "duplicate request_id %r across workspace "
                             "responses" % rid)
            elif rid is not None:
                seen_request_ids.add(rid)
            if not isinstance(m.get("ok"), bool):
                findings.hit("I10", i,
                             "workspace response missing boolean 'ok'")
            elif m.get("ok") is False and not m.get("message"):
                findings.hit("I10", i,
                             "workspace ok:false without 'message'")
            if not m.get("path"):
                findings.hit("I10", i,
                             "workspace response without 'path'")

    # ---- PROTOCOL 5 (v5, 2026-11-04): I12 agent lifecycle, I13 delegation ----
    # I12: an `agent` id is unique in one stream; start/end each exactly once;
    #      any block or ask carrying `agent` must sit after its start and before
    #      its end.  Without this the GUI can grow indented rows for an agent
    #      that never existed, or keep growing them after it said it was done --
    #      both give the user rows nobody can attribute.
    # I13: a block carrying `parent` must also carry `agent` (root blocks are
    #      never delegated), and `parent` must name an existing tool_call that
    #      belongs to the PARENT agent.  Depth is fixed at 1, so the parent is
    #      always the root -- i.e. that tool_call must carry no `agent` field.
    # Both apply only to streams declaring protocol >= 5; v4 fixtures predate
    # subagents and must not be judged against obligations that did not exist.
    v5_stream = isinstance(declared_protocol, int) and declared_protocol >= 5
    if v5_stream:
        started_agents, ended_agents = {}, {}
        for i, m in parsed:
            if m is None or m["t"] != "agent":
                continue
            aid, phase = m.get("id"), m.get("phase")
            if not isinstance(aid, str) or not aid:
                findings.hit("I12", i, "agent message without a non-empty 'id'")
                continue
            if phase == "start":
                if aid in started_agents:
                    findings.hit("I12", i, "duplicate agent start for %r" % aid)
                else:
                    started_agents[aid] = i
                if aid in ended_agents:
                    findings.hit("I12", i, "agent %r restarts after its end" % aid)
                if not m.get("name"):
                    findings.hit("I12", i, "agent start for %r without 'name'" % aid)
                # depth is fixed at 1: subagents may not spawn subagents
                if m.get("depth") != 1 or isinstance(m.get("depth"), bool):
                    findings.hit("I12", i, "agent %r depth must be 1 (got %r)"
                                 % (aid, m.get("depth")))
            elif phase == "end":
                if aid not in started_agents:
                    findings.hit("I12", i, "agent end for never-started %r" % aid)
                elif aid in ended_agents:
                    findings.hit("I12", i, "duplicate agent end for %r" % aid)
                else:
                    ended_agents[aid] = i
                # a failure/cancel still ends the agent -- but it must say why
                if not isinstance(m.get("ok"), bool):
                    findings.hit("I12", i, "agent end for %r without boolean 'ok'" % aid)
                if not m.get("summary"):
                    findings.hit("I12", i, "agent end for %r without 'summary'" % aid)
            else:
                findings.hit("I12", i, "agent %r has phase %r (must be start/end)"
                             % (aid, phase))
        for aid, line in started_agents.items():
            if aid not in ended_agents:
                findings.hit("I12", line, "agent %r started but never ended" % aid)

        for i, m in parsed:
            if m is None or m["t"] not in ("block_start", "ask"):
                continue
            aid = m.get("agent")
            if aid is None:
                continue
            if not isinstance(aid, str) or not aid:
                findings.hit("I12", i, "empty 'agent' field on %r" % m["t"])
                continue
            if aid not in started_agents:
                findings.hit("I12", i, "%r carries agent %r, which was never "
                                       "started" % (m["t"], aid))
            elif aid in ended_agents and i > ended_agents[aid]:
                findings.hit("I12", i, "%r carries agent %r after its end"
                             % (m["t"], aid))

        # owner[block_id] = the agent field of that tool_call (None = root)
        owner = {}
        for _i, m in parsed:
            if m is None or m["t"] != "block_start" or m.get("kind") != "tool_call":
                continue
            owner[m.get("id")] = m.get("agent")
        for i, m in parsed:
            if m is None or m["t"] != "block_start":
                continue
            parent = m.get("parent")
            if parent is None:
                continue
            if m.get("agent") is None:
                findings.hit("I13", i, "'parent' on a block without 'agent' -- "
                                      "root blocks are never delegated")
                continue
            if parent not in owner:
                findings.hit("I13", i, "'parent' names %r, which is not an "
                                      "existing tool_call" % parent)
                continue
            if owner[parent] is not None:
                findings.hit("I13", i, "'parent' %r belongs to agent %r; depth "
                                      "is fixed at 1, so the parent must be a "
                                      "root tool_call" % (parent, owner[parent]))


    # ---- PROTOCOL 5 (v6, 2026-11-05): I14 job lifecycle -------------------
    # A job id must be unique; its first message must be phase=start; output
    # and end only follow a start; end appears exactly once and nothing follows
    # it.  The start's `call` must name a tool_call block that already started
    # (the job card hangs under that call in the GUI).
    if isinstance(declared_protocol, int) and declared_protocol >= 6:
        job_started, job_ended = {}, {}
        for i, m in parsed:
            if m is None or m["t"] != "job":
                continue
            jid = m.get("id")
            phase = m.get("phase")
            if not isinstance(jid, str) or not jid:
                findings.hit("I14", i, "job without a non-empty 'id'")
                continue
            if phase == "start":
                if jid in job_started:
                    findings.hit("I14", i, "duplicate job start for %r" % jid)
                if not m.get("command") or not m.get("cwd"):
                    findings.hit("I14", i, "job start %r lacks command/cwd" % jid)
                if m.get("call") not in calls:
                    findings.hit("I14", i, "job start %r 'call' does not name "
                                           "a tool_call block: %r"
                                 % (jid, m.get("call")))
                job_started[jid] = i
            elif phase in ("output", "end"):
                if jid not in job_started:
                    findings.hit("I14", i, "job %s for never-started %r"
                                 % (phase, jid))
                elif jid in job_ended:
                    findings.hit("I14", i, "job %s after its end for %r"
                                 % (phase, jid))
                if phase == "end":
                    if m.get("status") not in ("exited", "failed", "killed",
                                                "timeout"):
                        findings.hit("I14", i, "job end %r has bad status %r"
                                     % (jid, m.get("status")))
                    job_ended[jid] = i
                elif not isinstance(m.get("text"), str) or not m.get("text"):
                    findings.hit("I14", i, "job output %r without text" % jid)
            else:
                findings.hit("I14", i, "job %r has phase %r (must be "
                                       "start/output/end)" % (jid, phase))
        for jid, line in job_started.items():
            if jid not in job_ended:
                findings.hit("I14", line, "job %r started but never ended" % jid)

    # PROTOCOL 9: a declared version above the supported ceiling means the GUI
    # would refuse to connect -- so a fixture claiming it cannot be "valid".
    # A MISSING field is not that case (it is not a claim of a higher version),
    # and the GUI treats it as v1, so it stays a note.
    if first is not None and first.get("t") == "hello":
        declared = first.get("protocol")
        if declared is None:
            findings.note("hello has no 'protocol' field -- treated as v1")
        elif not isinstance(declared, int) or declared > SUPPORTED_PROTOCOL_MAX:
            findings.hit("VERSION", 1,
                         "hello declares protocol %r, above the supported "
                         "maximum %d -- GUI refuses such a stream"
                         % (declared, SUPPORTED_PROTOCOL_MAX))

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


def is_resume(path, fixtures_root):
    rel = os.path.relpath(path, fixtures_root).replace("\\", "/")
    parts = rel.split("/")
    return "resume" in parts[0] or "resume" in os.path.basename(path).lower()


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
    parser.add_argument("--last-seq", type=int, default=None,
                        help="expected lastSeq for resume stream validation")
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
        resume = is_resume(path, opts.fixtures) or (opts.last_seq is not None)
        findings = Findings()
        lines = read_fixture(path, findings)
        parsed = parse(lines, findings)
        check_stream(parsed, findings, is_resume=resume, last_seq=opts.last_seq)
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
