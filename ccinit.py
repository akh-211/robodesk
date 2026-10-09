#!/usr/bin/env python3
"""ccinit: scaffold project hemat token untuk Claude Code + statusline pemandu.

  python ccinit.py new <nama> [--stack node|python|go|rust|generic]   # buat project siap pakai
  python ccinit.py init [--stack ...]       # pasang ke folder yang sudah ada (tidak menimpa file)
  python ccinit.py setup                    # pasang statusline ke ~/.claude (otomatis saat pertama kali)

Statusline TIDAK memakai token: hanya membaca JSON dari Claude Code, TASKS.md, dan git.
Env opsional: TG_FRESH=30000 TG_WARN=40000 TG_HIGH=80000 TG_CRIT=120000 NO_COLOR=1
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

# ----------------------------------------------------------------- util

def num(v, default=0.0):
    try:
        return float(v) if v is not None else default
    except (TypeError, ValueError):
        return default


def tok(n):
    n = int(n)
    return f"{n / 1000:.0f}k" if n >= 1000 else str(n)


def dur(sec):
    sec = int(max(sec, 0))
    h, rem = divmod(sec, 3600)
    if h >= 24:
        return f"{h // 24}d{h % 24}h"
    return f"{h}h{rem // 60:02d}m" if h else f"{rem // 60}m"


# ------------------------------------------------------------ templates

STACKS = {
    "node": {
        "desc": "Node.js / TypeScript", "run": "npm run dev",
        "test": "npm test -- <file>", "lint": "npm run lint",
        "dirs": ["node_modules", "dist", "build", ".next", "coverage"],
        "files": ["package-lock.json", "pnpm-lock.yaml", "yarn.lock"],
        "ignore": ["node_modules/", "dist/", ".next/", "coverage/", ".env"]},
    "python": {
        "desc": "Python", "run": "python -m <paket>",
        "test": "pytest tests/test_x.py -q", "lint": "ruff check .",
        "dirs": [".venv", "venv", "__pycache__", ".pytest_cache"], "files": [],
        "ignore": [".venv/", "venv/", "__pycache__/", ".pytest_cache/", "*.pyc", ".env"]},
    "go": {
        "desc": "Go", "run": "go run .", "test": "go test ./pkg/... -run Nama",
        "lint": "go vet ./...", "dirs": ["vendor"], "files": ["go.sum"],
        "ignore": ["bin/", ".env"]},
    "rust": {
        "desc": "Rust", "run": "cargo run", "test": "cargo test nama",
        "lint": "cargo clippy", "dirs": ["target"], "files": ["Cargo.lock"],
        "ignore": ["target/", ".env"]},
    "generic": {
        "desc": "[isi stack]", "run": "[perintah run]", "test": "[perintah test 1 file]",
        "lint": "[perintah lint]", "dirs": ["node_modules", "dist", "build", "coverage"],
        "files": [], "ignore": [".env"]},
}

CLAUDE_MD = """# {{name}}

<!-- Dimuat di SETIAP sesi: jaga < 60 baris. Prosedur panjang -> skills. -->

## Project (cek/isi; menghemat eksplorasi)
- Stack: {{desc}}
- Run: `{{run}}` | Test 1 file: `{{test}}` | Lint: `{{lint}}`
- Struktur: {{layout}}
- Konvensi: [hanya yang tidak jelas dari kode]

## Task-agent (1 task = 1 sesi)
Status task ada di TASKS.md (NOW / NEXT / DONE). Statusline membacanya untuk memberi tahu user kapan /clear, /compact, atau handoff, jadi JAGA FORMAT PERSIS (`- [ ]` / `- [x]`). Baca dengan `head -30`, jangan utuh.
- Permintaan kerja baru = 1 task: tulis 1 baris `- [ ] judul` di NOW pada langkah tool pertama (paralel dengan tool call lain). Lewati untuk tanya-jawab dan perubahan sepele.
- Selesai: centang `- [x]` di NOW dalam satu edit, paralel dengan verifikasi akhir. Tutup dengan satu baris: "Selesai."
- Task baru dari user saat NOW masih aktif: tambah ke NEXT saja, jangan dikerjakan.
- Saat memulai task berikutnya: pindahkan item `[x]` lama ke DONE (simpan maks 5) dalam edit yang sama.
- User ketik "lanjut": jika HANDOFF.md ada, baca itu saja lalu lanjutkan; jika NOW aktif, lanjutkan; jika tidak, pindahkan item pertama NEXT ke NOW lalu kerjakan.
- User ketik "handoff": tulis HANDOFF.md (maks 25 baris: tujuan, sudah selesai, langkah berikut, file tersentuh, jebakan, perintah persis), tanpa hal lain, lalu balas "Siap: /clear lalu ketik lanjut".
- Hapus HANDOFF.md saat task selesai. Jangan commit kecuali diminta.

## Membaca & mencari
- Grep/glob dulu, lalu baca hanya file atau rentang baris yang cocok. Jangan baca file besar utuh untuk orientasi.
- Jangan baca ulang file yang sudah ada di konteks. Abaikan: node_modules, dist, build, lockfile, generated, binary.
- Perlu memahami area besar? Delegasikan ke subagent, minta ringkasan maks 15 baris.

## Mengedit & menjalankan
- Diff sekecil mungkin. Tanpa refactor, rename, format, dokumen, atau test tambahan kecuali diminta. Edit terarah, bukan tulis ulang file.
- Permintaan ambigu: ajukan SATU pertanyaan singkat. Menyentuh 3+ file atau pendekatan belum jelas: rencana maks 5 poin, tunggu OK.
- Jalankan test/lint paling sempit; suite penuh hanya di akhir. Batasi output: `| tail -n 50` atau `grep -E "FAIL|ERROR"`.
- Perbaikan sama gagal 2x: berhenti, laporkan yang dicoba dan dugaan penyebab. Jangan looping.

## Balasan
- Singkat. Tanpa pembuka, tanpa mengulang tugas, tanpa rekap diff. Tampilkan hanya kode yang berubah + 1 baris cara verifikasi.

## Compact instructions
Saat compact, pertahankan: task di NOW, keputusan, path file yang diubah, error/test gagal, langkah berikut. Buang: jalan buntu, isi file penuh, log panjang.
"""

TASKS_MD = """# TASKS
<!-- Dibaca statusline. Dikelola Claude; jaga format: bagian NOW / NEXT / DONE, item `- [ ]` / `- [x]`. -->

## NOW

## NEXT

## DONE
"""

COMMON_IGNORE = ["HANDOFF.md", ".claude/settings.local.json"]
SKIP_DIRTY = ("TASKS.md", "HANDOFF.md")

# --------------------------------------------------------------- status

ANSI = re.compile(r"\x1b\[[0-9;]*m")


def read_tasks(proj):
    try:
        text = (proj / "TASKS.md").read_text("utf-8", errors="replace")[:20000]
    except OSError:
        return None
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    sec, now_items, nxt = "", [], []
    for line in text.splitlines():
        h = re.match(r"^#{1,6}\s*([A-Za-z]+)", line)
        if h:
            sec = h.group(1).upper()
            continue
        m = re.match(r"^\s*[-*]\s*\[( |x|X)\]\s*(.+)", line)
        if not m:
            continue
        done, title = m.group(1) != " ", m.group(2).strip()
        if sec == "NOW":
            now_items.append((done, title))
        elif sec == "NEXT" and not done:
            nxt.append(title)
    open_ = [t for d, t in now_items if not d]
    if open_:
        state, title = "active", open_[0]
    elif now_items:
        state, title = "done", now_items[-1][1]
    else:
        state, title = "none", ""
    return {"state": state, "title": title, "next": nxt[0] if nxt else None}


def git_info(proj, sid):
    name = "ccinit-" + re.sub(r"[^A-Za-z0-9_-]", "", sid)[:40] + ".json"
    cache = Path(tempfile.gettempdir()) / name
    try:
        if time.time() - cache.stat().st_mtime < 8:
            return json.loads(cache.read_text())
    except (OSError, ValueError):
        pass
    info = {"ok": False, "dirty": 0, "last": 0}

    def git(*a):
        return subprocess.run(["git", "-C", str(proj), *a], capture_output=True,
                              text=True, timeout=2)
    try:
        s = git("status", "--porcelain")
        if s.returncode == 0:
            info["ok"] = True
            for ln in s.stdout.splitlines():
                if ln.strip() and ln[3:].strip().strip('"') not in SKIP_DIRTY:
                    info["dirty"] += 1
            lg = git("log", "-1", "--format=%ct")
            if lg.returncode == 0 and lg.stdout.strip().isdigit():
                info["last"] = int(lg.stdout.strip())
    except (OSError, subprocess.SubprocessError):
        pass
    try:
        cache.write_text(json.dumps(info))
    except OSError:
        pass
    return info


def fit(segs, cols, sep):
    segs = list(segs)
    while len(segs) > 1 and len(ANSI.sub("", sep.join(t for _, t in segs))) > cols:
        worst = max(range(len(segs)), key=lambda i: (segs[i][0], i))
        del segs[worst]
    return sep.join(t for _, t in segs)


def cmd_status(_args=None):
    for stream in (sys.stdin, sys.stdout):
        try:
            stream.reconfigure(encoding="utf-8")
        except Exception:
            pass
    try:
        d = json.load(sys.stdin)
    except Exception:
        print("ccinit: tidak ada data")
        return 0
    now = time.time()
    nc = bool(os.environ.get("NO_COLOR"))

    def c(code):
        return "" if nc else f"\033[{code}m"
    RST, DIM, BLD, G, Y, R, CY = c(0), c(2), c(1), c(32), c(33), c(31), c(36)

    ws = d.get("workspace") or {}
    proj = Path(ws.get("project_dir") or ws.get("current_dir") or d.get("cwd") or ".")
    cw = d.get("context_window") or {}
    tokens = num(cw.get("total_input_tokens"))
    size = num(cw.get("context_window_size"), 200000) or 200000
    pct = num(cw.get("used_percentage"), tokens / size * 100)
    FRESH, WARN, HIGH, CRIT = (int(os.environ.get(k, v)) for k, v in (
        ("TG_FRESH", 30000), ("TG_WARN", 40000), ("TG_HIGH", 80000), ("TG_CRIT", 120000)))

    tasks = read_tasks(proj)
    g = git_info(proj, str(d.get("session_id") or "x"))
    started = now - num((d.get("cost") or {}).get("total_duration_ms")) / 1000
    handoff = (proj / "HANDOFF.md").exists()
    if tasks and tasks["state"] in ("active", "done"):
        state, title = tasks["state"], tasks["title"]
    elif g["ok"] and g["dirty"] == 0 and g["last"] and g["last"] + 1 >= started and tokens >= FRESH:
        state, title = "done", "commit baru, tree bersih"
    else:
        state, title = "none", ""
    nxt = tasks["next"] if tasks else None

    pc = d.get("prompt_cache") or {}
    observed = bool(pc.get("caching_observed"))
    left = num(pc.get("expires_at")) - now
    cold = observed and not (pc.get("warm") and left > 0)
    recache = pc.get("recache_tokens_if_cold")
    cold_note = f" (cache dingin, +{tok(recache)} diproses ulang)" if cold and recache else ""
    unc = f" · {g['dirty']} file belum commit" if g["ok"] and g["dirty"] else ""

    # ---- keputusan aksi (satu perintah, tanpa mikir)
    if tokens < FRESH:
        if handoff:
            line1 = f"{G}{BLD}➜ ketik: lanjut{RST}{G} — lanjutkan dari HANDOFF.md{RST}"
        elif state == "active":
            line1 = f"{G}✓ kerja: {title[:50]}{RST}"
        elif nxt:
            line1 = f"{G}{BLD}➜ ketik: lanjut{RST}{G} — mulai: {nxt[:45]}{RST}"
        else:
            line1 = f"{G}✓ siap — tulis task barumu{RST}"
    elif state == "done":
        nx = " lalu ketik: lanjut" if nxt else ""
        why = "cache dingin, /clear paling hemat" if cold else "task selesai"
        line1 = f"{Y}{BLD}➜ /clear{RST}{Y}{nx} — {why}{unc}{RST}"
    elif state == "active":
        if tokens >= CRIT or pct >= 80:
            line1 = f"{R}{BLD}➜ ketik: handoff{RST}{R} lalu /clear — konteks penuh, task belum selesai{cold_note}{RST}"
        elif tokens >= HIGH:
            line1 = f"{Y}{BLD}➜ /compact{RST}{Y} — konteks besar, task masih jalan{cold_note}{RST}"
        else:
            hint = f"{DIM} (konteks mulai besar){RST}" if tokens >= WARN else ""
            line1 = f"{G}✓ lanjut kerja{RST}{hint}"
    else:
        if tokens >= WARN:
            line1 = f"{Y}{BLD}➜ /clear{RST}{Y} — tidak ada task aktif{cold_note}{RST}"
        else:
            line1 = f"{G}✓ OK{RST}"
    five = (d.get("rate_limits") or {}).get("five_hour") or {}
    if num(five.get("used_percentage")) >= 90:
        line1 += f" {R}· limit 5h {num(five['used_percentage']):.0f}%, tunda tugas besar{RST}"

    # ---- baris info (dipangkas sesuai lebar terminal)
    model = (d.get("model") or {}).get("display_name", "?")
    effort = (d.get("effort") or {}).get("level")
    col = R if tokens >= CRIT or pct >= 80 else Y if tokens >= HIGH else G
    filled = min(10, int(pct // 10))
    bar = "█" * filled + "░" * (10 - filled)
    segs = [(1, f"{CY}[{model}{'·' + effort if effort else ''}]{RST}"),
            (1, f"{col}{bar}{RST} {pct:.0f}% {tok(tokens)}")]
    if state == "active":
        segs.append((2, f"▶ {title[:30]}"))
    elif state == "done":
        segs.append((2, f"✔ {title[:30]}"))
    if observed:
        if cold:
            segs.append((3, f"{R}cache dingin{RST}"))
        else:
            segs.append((3, f"{G if left > 300 else Y}cache {dur(left)}{RST}"))
    rl = d.get("rate_limits") or {}
    for prio, key, lab in ((3, "five_hour", "5h"), (5, "seven_day", "7d")):
        w = rl.get(key) or {}
        if w.get("used_percentage") is not None:
            p = num(w["used_percentage"])
            cc = R if p >= 85 else Y if p >= 60 else G
            segs.append((prio, f"{cc}{lab} {p:.0f}%{RST}"))
    if g["ok"] and g["dirty"]:
        segs.append((4, f"git ±{g['dirty']}"))
    cols = int(num(os.environ.get("COLUMNS"), 0)) or 110
    print(line1)
    print(fit(segs, cols, f"{DIM} │ {RST}"))
    return 0


# ---------------------------------------------------------------- setup

def claude_home():
    return Path.home() / ".claude"


def read_json(p):
    if not p.exists():
        return {}
    return json.loads(p.read_text("utf-8") or "{}")


def statusline_state():
    try:
        sl = read_json(claude_home() / "settings.json").get("statusLine")
    except ValueError:
        return "broken"
    if not sl:
        return "none"
    return "ours" if "ccinit.py" in str(sl.get("command", "")) else "other"


def cmd_setup(_args=None):
    home = claude_home()
    home.mkdir(parents=True, exist_ok=True)
    dst = home / "ccinit.py"
    src = Path(__file__).resolve()
    if not dst.exists() or src != dst.resolve():
        shutil.copy2(src, dst)
    sp = home / "settings.json"
    try:
        data = read_json(sp)
    except ValueError:
        print(f"✗ {sp} bukan JSON valid. Perbaiki dulu; file tidak diubah.")
        return 1
    if sp.exists():
        bak = sp.with_name("settings.json.ccinit-bak")
        if not bak.exists():
            shutil.copy2(sp, bak)
    old = data.get("statusLine")
    cmd = f'"{Path(sys.executable).as_posix()}" "{dst.as_posix()}" status'
    data["statusLine"] = {"type": "command", "command": cmd, "refreshInterval": 30}
    data.setdefault("env", {}).setdefault("CLAUDE_CODE_GOAL_CHECKIN_MINUTES", "0")
    data.setdefault("crossSessionInbound", "hold")
    sp.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", "utf-8")
    print(f"✓ Statusline terpasang: {sp}")
    if old and "ccinit.py" not in str(old.get("command", "")):
        print(f"  (statusLine lama diganti; backup: {sp.name}.ccinit-bak)")
    print("  Restart Claude Code agar aktif.")
    return 0


def ensure_statusline():
    st = statusline_state()
    if st == "ours":
        me, dst = Path(__file__).resolve(), claude_home() / "ccinit.py"
        if dst.exists() and me != dst.resolve() and me.stat().st_mtime > dst.stat().st_mtime:
            shutil.copy2(me, dst)  # versi yang lebih baru ikut menggantikan salinan global
        return ""
    if st == "none":
        cmd_setup()
        return "Pertama kali: statusline otomatis dipasang ke ~/.claude."
    if st == "other":
        return "Kamu sudah punya statusline lain. Untuk menggantinya: python ccinit.py setup"
    return "~/.claude/settings.json tidak valid; statusline dilewati."


# ------------------------------------------------------------- scaffold

def detect_stack(p):
    for f, s in (("package.json", "node"), ("pyproject.toml", "python"),
                 ("requirements.txt", "python"), ("go.mod", "go"), ("Cargo.toml", "rust")):
        if (p / f).exists():
            return s
    return "generic"


def scaffold(root, stack, name, new):
    S = STACKS[stack]
    made, skipped = [], []

    def put(rel, text):
        f = root / rel
        if f.exists():
            skipped.append(rel)
            return False
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text(text, "utf-8")
        made.append(rel)
        return True

    md = CLAUDE_MD
    for k, v in {"name": name, "desc": S["desc"], "run": S["run"], "test": S["test"],
                 "lint": S["lint"],
                 "layout": "`src/` kode, `tests/` test" if new else "[isi folder utama]"}.items():
        md = md.replace("{{" + k + "}}", v)
    if not put("CLAUDE.md", md):
        (root / "CLAUDE.ccinit.md").write_text(md, "utf-8")
    put("TASKS.md", TASKS_MD)

    # permissions.deny: blokir folder/file bising (merge, tidak menimpa)
    deny = [f"Read(./{x}/**)" for x in S["dirs"]] + [f"Read(./{x})" for x in S["files"]]
    sp = root / ".claude" / "settings.json"
    try:
        data = read_json(sp)
        cur = data.setdefault("permissions", {}).setdefault("deny", [])
        add = [x for x in deny if x not in cur]
        if add or not sp.exists():
            cur.extend(add)
            sp.parent.mkdir(parents=True, exist_ok=True)
            sp.write_text(json.dumps(data, indent=2) + "\n", "utf-8")
            made.append(".claude/settings.json")
    except ValueError:
        skipped.append(".claude/settings.json (bukan JSON valid)")

    # .gitignore: tambahkan baris yang belum ada
    gi = root / ".gitignore"
    have = gi.read_text("utf-8").splitlines() if gi.exists() else []
    add = [x for x in S["ignore"] + COMMON_IGNORE if x not in have]
    if add:
        with gi.open("a", encoding="utf-8") as fh:
            fh.write(("\n" if have else "") + "\n".join(add) + "\n")
        made.append(".gitignore")

    if new:
        for d in ("src", "tests"):
            (root / d).mkdir(exist_ok=True)
            (root / d / ".gitkeep").touch()
            made.append(d + "/")
    return made, skipped


def git_init(root):
    if not shutil.which("git"):
        return "git tidak ditemukan (dilewati)"

    def run(*a):
        return subprocess.run(["git", "-C", str(root), *a], capture_output=True, text=True)
    run("init", "-q")
    run("add", "-A")
    if run("commit", "-q", "-m", "chore: scaffold ccinit").returncode:
        return "git init OK, commit awal dilewati (set git user.name/email dulu)"
    return "git init + commit awal OK"


def report(title, made, skipped, extra):
    print(title)
    if made:
        print("  dibuat  : " + "  ".join(made))
    if skipped:
        print("  dilewati: " + "  ".join(skipped) + "  (sudah ada, tidak ditimpa)")
    for e in extra:
        if e:
            print("  " + e)


def cmd_new(a):
    root = Path(a.name)
    if root.exists() and any(root.iterdir()):
        print(f"✗ '{root}' sudah ada dan tidak kosong. Untuk folder yang sudah ada: cd ke sana, lalu 'init'.")
        return 1
    root.mkdir(parents=True, exist_ok=True)
    stack = a.stack or "generic"
    made, skipped = scaffold(root, stack, root.resolve().name, new=True)
    note = git_init(root)
    hint = ensure_statusline()
    report(f"✓ Project siap: {root} (stack: {stack})", made, skipped, [note, hint])
    print(f"\nMulai:  cd {root} && claude")
    print("Lalu tulis task-mu langsung. Ikuti saja perintah di baris atas statusline (➜ ...).")
    return 0


def cmd_init(a):
    root = Path.cwd()
    stack = a.stack or detect_stack(root)
    made, skipped = scaffold(root, stack, root.name, new=False)
    hint = ensure_statusline()
    report(f"✓ Terpasang di {root} (stack: {stack})", made, skipped, [hint])
    if "CLAUDE.md" in skipped:
        print("  CLAUDE.md sudah ada: template disimpan di CLAUDE.ccinit.md, gabungkan bagian 'Task-agent'.")
    print("\nMulai:  claude   (ikuti perintah di baris atas statusline)")
    return 0


def main():
    ap = argparse.ArgumentParser(prog="ccinit", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd")
    n = sub.add_parser("new", help="buat project baru siap pakai")
    n.add_argument("name")
    n.add_argument("--stack", choices=list(STACKS))
    n.set_defaults(fn=cmd_new)
    i = sub.add_parser("init", help="pasang di folder yang sudah ada")
    i.add_argument("--stack", choices=list(STACKS))
    i.set_defaults(fn=cmd_init)
    sub.add_parser("setup", help="pasang statusline ke ~/.claude").set_defaults(fn=cmd_setup)
    sub.add_parser("status").set_defaults(fn=cmd_status)
    a = ap.parse_args()
    if not a.cmd:
        ap.print_help()
        return 0
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
