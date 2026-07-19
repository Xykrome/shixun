#!/usr/bin/env python3
"""Quick diagnostic test for V1.3."""
import subprocess, time, os

os.chdir("/home/wamdus/shixun/mini_webserver")
subprocess.run(["pkill", "-f", "mini_web_server"], capture_output=True)
time.sleep(0.5)
proc = subprocess.Popen(["./mini_web_server", "serve-http", "6"],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
time.sleep(1)

def curl(url, data=None):
    cmd = ["curl", "-s", "--max-time", "5"]
    if data:
        cmd += ["-X", "POST", "-H", "Content-Type: application/x-www-form-urlencoded", "-d", data]
    cmd.append(url)
    r = subprocess.run(cmd, capture_output=True, timeout=10)
    return r.stdout  # raw bytes

# Test 2: full response body
print("=== FULL BODY for GET /search?class=2011&keyword=%E7%94%B7 ===")
body = curl("http://127.0.0.1:8080/search?class=2011&keyword=%E7%94%B7")
print(body.decode('utf-8', errors='replace'))

print("\n=== RAW bytes around table area ===")
idx = body.find(b'<hr>')
if idx >= 0:
    print(body[idx:idx+400])

print("\n=== Searching data file directly ===")
with open("data/2011.txt", "rb") as f:
    for line in f:
        if b'\xe7\x94\xb7' in line:  # 男 in UTF-8
            print("MATCH:", line.decode('utf-8').rstrip())

proc.terminate()
proc.wait()
print("\nDone.")
