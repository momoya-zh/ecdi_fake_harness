#!/usr/bin/env python3
"""verify_core_l3.py -- verification tests for L3 memory loading in fake-core / ecdi-core."""

import json
import os
import shutil
import subprocess
import sys
import tempfile

FAKE_CORE = os.path.abspath("core/bin/fake-core.exe")


def run_core_single_round(args, cwd, stdin_lines):
    cmd = [FAKE_CORE] + args
    p = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=cwd,
        text=True,
        encoding="utf-8",
        errors="replace"
    )
    stdout, stderr = p.communicate(input="\n".join(stdin_lines) + "\n", timeout=10)
    lines = [line.strip() for line in stdout.split("\n") if line.strip()]
    parsed = []
    for line in lines:
        try:
            parsed.append(json.loads(line))
        except Exception:
            pass
    return parsed, stderr


def test_1_negative_control_no_memory():
    print("[Test 1] Negative control: without memory dirs, no memories in hello and no notices")
    tmp = tempfile.mkdtemp(prefix="ecdi_l3_test1_")
    try:
        parsed, stderr = run_core_single_round(["--workspace", tmp], tmp, [
            json.dumps({"t": "hello", "seq": 0}),
            json.dumps({"t": "bye", "seq": 1})
        ])
        hello = parsed[0]
        assert hello["t"] == "hello"
        assert "memories" not in hello, f"unexpected memories in hello: {hello}"
        for m in parsed:
            if m["t"] == "block_start" and m.get("kind") == "system":
                txt = m.get("text", "")
                assert "记忆" not in txt, f"unexpected memory notice: {txt}"
        print("  -> PASS: no memories in hello, zero memory notices")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_2_normal_loading_and_hello():
    print("[Test 2] Normal loading: workspace .ecdi/memory/*.md appears in hello")
    tmp = tempfile.mkdtemp(prefix="ecdi_l3_test2_")
    try:
        mem_dir = os.path.join(tmp, ".ecdi", "memory")
        os.makedirs(mem_dir, exist_ok=True)
        with open(os.path.join(mem_dir, "rule.md"), "w", encoding="utf-8") as f:
            f.write("# Project rules\nAlways be concise.")

        parsed, stderr = run_core_single_round(["--workspace", tmp], tmp, [
            json.dumps({"t": "hello", "seq": 0}),
            json.dumps({"t": "bye", "seq": 1})
        ])
        hello = parsed[0]
        assert "memories" in hello, f"memories missing from hello: {hello}"
        mems = hello["memories"]
        assert len(mems) == 1, f"expected 1 memory item, got {mems}"
        assert mems[0]["name"] == "rule.md"
        assert mems[0]["source"] == "workspace"
        assert mems[0]["skipped"] == False
        print("  -> PASS: hello.memories has rule.md (source: workspace, skipped: false)")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_3_budget_limit_skip_and_notice():
    print("[Test 3] 10% budget limit skips oversized file and emits kind:system notice")
    tmp = tempfile.mkdtemp(prefix="ecdi_l3_test3_")
    try:
        mem_dir = os.path.join(tmp, ".ecdi", "memory")
        os.makedirs(mem_dir, exist_ok=True)
        # context = 1000 tokens => 10% = 100 tokens => 400 bytes budget
        # 1. 1_fit.md: 100 bytes (25 tokens)
        with open(os.path.join(mem_dir, "1_fit.md"), "w", encoding="utf-8") as f:
            f.write("A" * 100)
        # 2. 2_huge.md: 350 bytes (88 tokens) -> 25+88=113 > 100 -> SKIP!
        with open(os.path.join(mem_dir, "2_huge.md"), "w", encoding="utf-8") as f:
            f.write("B" * 350)

        parsed, stderr = run_core_single_round(["--workspace", tmp, "--context", "1000"], tmp, [
            json.dumps({"t": "hello", "seq": 0}),
            json.dumps({"t": "bye", "seq": 1})
        ])
        hello = parsed[0]
        mems = hello.get("memories", [])
        assert len(mems) == 2, f"expected 2 files in memories: {mems}"
        assert mems[0]["name"] == "1_fit.md" and mems[0]["skipped"] == False
        assert mems[1]["name"] == "2_huge.md" and mems[1]["skipped"] == True

        # Check for system notice: [记忆文件超限已跳过: 2_huge.md]
        found_skip_notice = False
        for m in parsed:
            if m["t"] == "block_start" and m.get("kind") == "system":
                txt = m.get("text", "")
                if "超限已跳过" in txt and "2_huge.md" in txt:
                    found_skip_notice = True
                    break
        assert found_skip_notice, f"expected skip notice not found in stream: {parsed}"
        print("  -> PASS: 2_huge.md skipped in hello, system notice emitted")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_4_resume_notice():
    print("[Test 4] Resume session emits [记忆已更新] when memories exist")
    tmp = tempfile.mkdtemp(prefix="ecdi_l3_test4_")
    try:
        mem_dir = os.path.join(tmp, ".ecdi", "memory")
        os.makedirs(mem_dir, exist_ok=True)
        with open(os.path.join(mem_dir, "note.md"), "w", encoding="utf-8") as f:
            f.write("Memory note")

        sess_dir = os.path.join(tmp, ".ecdi", "sessions")
        os.makedirs(sess_dir, exist_ok=True)
        meta = {"id": "s-test-resume", "lastSeq": 10, "createdAt": 100, "lastUsedAt": 100}
        with open(os.path.join(sess_dir, "s-test-resume.meta.json"), "w", encoding="utf-8") as f:
            json.dump(meta, f)
        with open(os.path.join(sess_dir, "s-test-resume.context.jsonl"), "w", encoding="utf-8") as f:
            f.write(json.dumps({"t": "msg", "role": "user", "content": "hi"}) + "\n")

        parsed, stderr = run_core_single_round(["--workspace", tmp, "--resume", "s-test-resume"], tmp, [
            json.dumps({"t": "hello", "seq": 11}),
            json.dumps({"t": "bye", "seq": 12})
        ])
        found_resume_notice = False
        for m in parsed:
            if m["t"] == "block_start" and m.get("kind") == "system":
                if m.get("text") == "[记忆已更新]":
                    found_resume_notice = True
                    break
        assert found_resume_notice, f"expected [记忆已更新] notice in stream: {parsed}"
        print("  -> PASS: [记忆已更新] notice emitted on resume")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    test_1_negative_control_no_memory()
    test_2_normal_loading_and_hello()
    test_3_budget_limit_skip_and_notice()
    test_4_resume_notice()
    print("\nALL 4 L3 CORE VERIFICATION TESTS PASSED SUCCESSFULLY!")


if __name__ == "__main__":
    main()
