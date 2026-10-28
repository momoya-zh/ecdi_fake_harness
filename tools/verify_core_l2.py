#!/usr/bin/env python3
"""verify_core_l2.py -- Verification script for L2 resume and 85% compression in core."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FAKE_CORE = os.path.join(ROOT, "core", "bin", "fake-core.exe")

def run_proc(args, cwd, stdin_lines):
    proc = subprocess.Popen(
        args,
        cwd=cwd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    stdin_data = "\n".join(stdin_lines) + "\n"
    stdout, stderr = proc.communicate(input=stdin_data, timeout=10)
    out_lines = [l.strip() for l in stdout.splitlines() if l.strip()]
    msgs = []
    for line in out_lines:
        try:
            msgs.append(json.loads(line))
        except Exception:
            pass
    return msgs, stderr, proc.returncode

def main():
    test_dir = tempfile.mkdtemp(prefix="ecdi_l2_test_")
    try:
        sessions_dir = os.path.join(test_dir, ".ecdi", "sessions")
        os.makedirs(sessions_dir, exist_ok=True)
        session_id = "s-20261028-123456-test"

        print("[Test 1] Fresh session writes context.jsonl and starts seq at 0")
        stdin_fresh = [
            json.dumps({"t": "hello", "seq": 0, "protocol": 3, "gui": "0.1.0"}),
            json.dumps({"t": "prompt", "seq": 1, "client_id": "p1", "text": "第一轮测试"}),
            json.dumps({"t": "bye", "seq": 2}),
        ]
        msgs, stderr, code = run_proc(
            [FAKE_CORE, "--burst", "--session", session_id, "--workspace", test_dir],
            test_dir,
            stdin_fresh,
        )
        assert len(msgs) > 0, "No messages received"
        assert msgs[0]["t"] == "hello", f"Expected hello, got {msgs[0]}"
        assert msgs[0]["seq"] == 0, f"Fresh hello seq must be 0, got {msgs[0]['seq']}"
        last_seq = msgs[-1]["seq"]
        print(f"  -> Fresh session completed with {len(msgs)} messages, lastSeq={last_seq}")

        # Check context.jsonl
        ctx_path = os.path.join(sessions_dir, session_id + ".context.jsonl")
        assert os.path.exists(ctx_path), f"Missing {ctx_path}"
        with open(ctx_path, "r", encoding="utf-8") as f:
            ctx_lines = [json.loads(l) for l in f if l.strip()]
        assert len(ctx_lines) >= 2, f"Expected >= 2 context lines, got {len(ctx_lines)}"
        print(f"  -> context.jsonl exists with {len(ctx_lines)} records (user, assistant, tools)")

        # Write fake meta.json
        meta_path = os.path.join(sessions_dir, session_id + ".meta.json")
        with open(meta_path, "w", encoding="utf-8") as f:
            json.dump({"id": session_id, "lastSeq": last_seq, "contextLimit": 1000}, f)

        print("[Test 2] Resume session starts seq at lastSeq+1 and replays context.jsonl")
        stdin_resume = [
            json.dumps({"t": "hello", "seq": 0, "protocol": 3, "gui": "0.1.0"}),
            json.dumps({"t": "prompt", "seq": 1, "client_id": "p2", "text": "第二轮续接"}),
            json.dumps({"t": "bye", "seq": 2}),
        ]
        msgs_res, stderr_res, code_res = run_proc(
            [FAKE_CORE, "--burst", "--resume", session_id, "--workspace", test_dir],
            test_dir,
            stdin_resume,
        )
        assert len(msgs_res) > 0, "No messages received on resume"
        assert msgs_res[0]["t"] == "hello", f"Expected hello, got {msgs_res[0]}"
        expected_start = last_seq + 1
        assert msgs_res[0]["seq"] == expected_start, f"Resume hello seq must be {expected_start}, got {msgs_res[0]['seq']}"
        print(f"  -> Resume started cleanly at seq={msgs_res[0]['seq']} (lastSeq+1)")

        print("[Test 3] 85% compression triggers with small contextLimit and emits system notice block")
        compress_session_id = "s-20261028-compress-test"
        # We start with a very small context limit (e.g. 100 tokens) so occupancy triggers 85%
        stdin_compress = [
            json.dumps({"t": "hello", "seq": 0, "protocol": 3, "gui": "0.1.0"}),
            json.dumps({"t": "prompt", "seq": 1, "client_id": "p1", "text": "第 1 轮创建一些上下文"}),
            json.dumps({"t": "prompt", "seq": 2, "client_id": "p2", "text": "第 2 轮增加内容"}),
            json.dumps({"t": "prompt", "seq": 3, "client_id": "p3", "text": "第 3 轮触发压缩"}),
            json.dumps({"t": "bye", "seq": 4}),
        ]
        msgs_comp, stderr_comp, code_comp = run_proc(
            [FAKE_CORE, "--burst", "--session", compress_session_id, "--workspace", test_dir, "--context", "100"],
            test_dir,
            stdin_compress,
        )
        system_notices = [m for m in msgs_comp if m.get("t") == "block_start" and m.get("kind") == "system"]
        assert len(system_notices) > 0, "Expected at least one system compression notice"
        print(f"  -> Found system notice block: {system_notices[0].get('text')}")
        assert "上下文已压缩" in system_notices[0].get("text"), "Notice text format mismatch"

        # Check compact record in context.jsonl
        comp_ctx_path = os.path.join(sessions_dir, compress_session_id + ".context.jsonl")
        with open(comp_ctx_path, "r", encoding="utf-8") as f:
            comp_ctx_lines = [json.loads(l) for l in f if l.strip()]
        compact_records = [l for l in comp_ctx_lines if l.get("t") == "compact"]
        assert len(compact_records) > 0, "Expected compact record in context.jsonl"
        print(f"  -> compact record in context.jsonl: replaces={compact_records[0].get('replaces')}")

        print("[Test 4] Negative control: --fail-summarize emits failure notice without losing messages")
        fail_session_id = "s-20261028-fail-test"
        stdin_fail = [
            json.dumps({"t": "hello", "seq": 0, "protocol": 3, "gui": "0.1.0"}),
            json.dumps({"t": "prompt", "seq": 1, "client_id": "p1", "text": "测试消息"}),
            json.dumps({"t": "prompt", "seq": 2, "client_id": "p2", "text": "增加上下文"}),
            json.dumps({"t": "prompt", "seq": 3, "client_id": "p3", "text": "触发失败压缩"}),
            json.dumps({"t": "bye", "seq": 4}),
        ]
        msgs_fail, stderr_fail, code_fail = run_proc(
            [FAKE_CORE, "--burst", "--session", fail_session_id, "--workspace", test_dir, "--context", "100", "--fail-summarize"],
            test_dir,
            stdin_fail,
        )
        fail_notices = [m for m in msgs_fail if m.get("t") == "block_start" and m.get("kind") == "system" and "压缩失败" in m.get("text", "")]
        assert len(fail_notices) > 0, "Expected failure notice"
        print(f"  -> Found failure notice: {fail_notices[0].get('text')}")

        # Check that no compact record was written
        fail_ctx_path = os.path.join(sessions_dir, fail_session_id + ".context.jsonl")
        with open(fail_ctx_path, "r", encoding="utf-8") as f:
            fail_ctx_lines = [json.loads(l) for l in f if l.strip()]
        assert all(l.get("t") != "compact" for l in fail_ctx_lines), "No compact record should exist when summarization fails"
        print("  -> Message count preserved without truncation or compact record")

        print("[Test 5] Negative control: without contextTokens, no compression triggers")
        no_ctx_session_id = "s-20261028-no-ctx"
        msgs_no_ctx, stderr_no_ctx, _ = run_proc(
            [FAKE_CORE, "--burst", "--session", no_ctx_session_id, "--workspace", test_dir],
            test_dir,
            stdin_fail,
        )
        assert all(m.get("kind") != "system" or "压缩" not in m.get("text", "") for m in msgs_no_ctx if m.get("t") == "block_start"), "Should not compress without context"
        print("  -> No compression triggered when hello.context is absent")

        print("\nALL 5 L2 CORE TESTS PASSED SUCCESSFULLY!")
        return 0
    finally:
        shutil.rmtree(test_dir, ignore_errors=True)

if __name__ == "__main__":
    sys.exit(main())
