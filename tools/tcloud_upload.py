#!/usr/bin/env python3
"""Sube un fichero a TCloud por streaming (1 MB de RAM). Solo stdlib."""
import argparse, http.client, json, mimetypes, os, sys, time
import urllib.error, urllib.parse, urllib.request, uuid

CHUNK = 1 << 20


def _field(b, name, value):
    return (f'--{b}\r\nContent-Disposition: form-data; name="{name}"'
            f"\r\n\r\n{value}\r\n").encode()


def _file_head(b, name, filename, ctype):
    return (f'--{b}\r\nContent-Disposition: form-data; name="{name}";'
            f' filename="{filename}"\r\nContent-Type: {ctype}\r\n\r\n').encode()


def enqueue(base, key, path, chat, topic=0, caption="", lane="low", front=False):
    u = urllib.parse.urlsplit(base)
    host, port = u.hostname, u.port or (443 if u.scheme == "https" else 80)
    conn_cls = http.client.HTTPSConnection if u.scheme == "https" else http.client.HTTPConnection
    b = "----tcloud" + uuid.uuid4().hex
    name = os.path.basename(path)
    head = _field(b, "chat_id", chat)
    if topic:
        head += _field(b, "topic_id", str(topic))
    if caption:
        head += _field(b, "caption", caption)
    if lane:
        head += _field(b, "lane", lane)
    if front:
        head += _field(b, "front", "true")
    ctype = mimetypes.guess_type(name)[0] or "application/octet-stream"
    head += _file_head(b, "file", name, ctype)
    tail = f"\r\n--{b}--\r\n".encode()
    size = os.path.getsize(path)
    conn = conn_cls(host, port, timeout=900)
    try:
        conn.putrequest("POST", "/v1/uploads")
        conn.putheader("X-API-Key", key)
        conn.putheader("Content-Type", f"multipart/form-data; boundary={b}")
        conn.putheader("Content-Length", str(len(head) + size + len(tail)))
        conn.endheaders()
        conn.send(head)
        with open(path, "rb") as f:
            while True:
                chunk = f.read(CHUNK)      # 1 MB como máximo en RAM
                if not chunk:
                    break
                conn.send(chunk)
        conn.send(tail)
        resp = conn.getresponse()
        raw = resp.read()
        try:
            return resp.status, json.loads(raw.decode() or "{}")
        except ValueError:
            return resp.status, {"raw": raw[:300].decode(errors="replace")}
    finally:
        conn.close()


def get_json(url, key):
    req = urllib.request.Request(url, headers={"X-API-Key": key})
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            return r.status, json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        raw = e.read()
        try:
            return e.code, json.loads(raw.decode())
        except ValueError:
            return e.code, {"raw": raw[:300].decode(errors="replace")}


def msg_link(chat, topic, msg_id):
    s = str(chat or "")
    if s.startswith("@") and len(s) > 1:
        return f"https://t.me/{s[1:]}/{msg_id}"
    if s.startswith("-100") and s[1:].isdigit():
        base = f"https://t.me/c/{s[4:]}"
        return f"{base}/{topic}/{msg_id}" if topic else f"{base}/{msg_id}"
    return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--chat", default=os.environ.get("TCLOUD_CHAT", ""))
    ap.add_argument("--topic", type=int, default=int(os.environ.get("TCLOUD_TOPIC", "0") or 0))
    ap.add_argument("--caption", default="")
    ap.add_argument("--lane", default="low", choices=["low", "normal", "high"])
    ap.add_argument("--front", action="store_true")
    ap.add_argument("--key", default=os.environ.get("TCLOUD_KEY", ""))
    ap.add_argument("--url", default=os.environ.get("TCLOUD_URL", "http://192.168.88.115:8931"))
    a = ap.parse_args()
    if not a.key or not a.chat:
        print("ERROR: faltan TCLOUD_KEY / --chat", file=sys.stderr)
        return 2
    if not os.path.isfile(a.path):
        print(f"ERROR: no existe {a.path}", file=sys.stderr)
        return 2
    base = a.url.rstrip("/")
    st, d = enqueue(base, a.key, a.path, a.chat, a.topic, a.caption, a.lane, a.front)
    if st != 201:
        print(f"ERROR al encolar: {st} {d}", file=sys.stderr)
        return 1
    job, topic = d["job_id"], d.get("topic_id") or a.topic
    print(f"job {job} · {os.path.getsize(a.path)} bytes · topic {topic}")
    while True:
        st, d = get_json(f"{base}/v1/uploads/{job}?wait_ms=25000", a.key)
        if st == 202:
            print("  subiendo", (d.get("progress") or {}).get("pct"), "%", flush=True)
            continue
        if st == 200:
            mid = d["msg_id"]
            print(f"OK msg_id={mid} · {d.get('progress', {}).get('done')} bytes")
            link = msg_link(d.get("chat") or a.chat, d.get("topic_id") or topic, mid)
            if link:
                print(link)
            return 0
        print(f"ERROR: {st} {d}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
