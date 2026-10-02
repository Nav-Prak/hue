# tests/reload_smoke.py
"""Exercise real reloads in an isolated executable copy, never the user's assets."""
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time

def command(sock, text):
    sock.sendall((text + "\n").encode())
    data = b""
    while not data.endswith(b"\n"):
        part = sock.recv(16384)
        if not part:
            raise RuntimeError("engine disconnected")
        data += part
        if len(data) > 65536:
            raise RuntimeError("oversized response")
    return json.loads(data)

def main():
    source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="hue-reload-") as directory:
        root = Path(directory)
        # Copy only runtime inputs, avoiding PDBs/toolchain/source paths.
        shutil.copy2(source, root / source.name)
        for dll in source.parent.glob("*.dll"):
            shutil.copy2(dll, root / dll.name)
        for folder in ("assets", "shaders"):
            shutil.copytree(source.parent / folder, root / folder)
        with socket.socket() as reserve:
            reserve.bind(("127.0.0.1", 0))
            port = reserve.getsockname()[1]
        with tempfile.TemporaryFile() as log:
            proc = subprocess.Popen([str(root / source.name), "--debug-channel", str(port)], cwd=root, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 25
                sock = None
                while time.monotonic() < deadline and proc.poll() is None:
                    try:
                        sock = socket.create_connection(("127.0.0.1", port), timeout=10)
                        break
                    except OSError:
                        time.sleep(0.1)
                assert sock is not None, "startup failed"
                with sock:
                    assert command(sock,"fight.start")["ok"]
                    time.sleep(0.05)
                    tuning_path = root / "assets/config/tuning.json"
                    tuning = json.loads(tuning_path.read_text())
                    tuning["revision"] = 2
                    tuning["cancel_scale"][0] = 0.5
                    tuning_path.write_text(json.dumps(tuning))
                    assert command(sock,"tuning.reload")["ok"]
                    active = command(sock,"fight.status")["data"]
                    assert active["revision"] == 1 and active["tuning_pending"]
                    tuning_path.write_text('{"version":')
                    assert not command(sock,"tuning.reload")["ok"]
                    assert command(sock,"fight.status")["data"]["tuning_pending"]
                    assert command(sock,"fight.reset")["ok"]
                    time.sleep(0.05)
                    assert command(sock,"fight.status")["data"]["revision"] == 2
                    scene_path = root / "assets/config/arena.json"
                    scene = json.loads(scene_path.read_text())
                    scene["player_spawn"][0] = -2
                    scene_path.write_text(json.dumps(scene))
                    assert command(sock,"scene.reload")["ok"]
                    scene_path.write_text('{}')
                    assert not command(sock,"scene.reload")["ok"]
                    assert command(sock,"fight.reset")["ok"]
                    time.sleep(0.05)
                    assert command(sock,"fight.status")["data"]["player_x"] == -2
                    render = command(sock,"render.status")
                    if render["ok"]:
                        target = root / "shaders/mesh.vert.spv"
                        good = target.read_bytes()
                        wrong_abi = (root / "shaders/hud.vert.spv").read_bytes()
                        times = []
                        initial_frames = render["data"]["frames_rendered"]
                        baseline = None
                        for i in range(20):
                            target.write_bytes(wrong_abi if i % 2 else good[:20])
                            assert not command(sock,"shader.reload")["ok"]
                            target.write_bytes(good)
                            start = time.monotonic()
                            assert command(sock,"shader.reload")["ok"]
                            times.append(time.monotonic()-start)
                            status = command(sock,"render.status")["data"]
                            assert (status["pipelines"],status["layouts"],status["modules"]) == (4,3,0), status
                            assert status["generation"] == i+1, status
                            tags = command(sock,"mem.snapshot")["data"]["tags"]
                            live = {t["tag"]:t["current_bytes"] for t in tags if t["tag"] in ("render","animation","physics","assets")}
                            if i == 3: baseline = live
                            if i > 3: assert live == baseline, (live,baseline)
                        assert status["frames_rendered"] > initial_frames
                        print(f"GPU reloads: 20 accepted + 20 rejected; max response {max(times):.3f}s; handles and tagged live bytes stable")
                    else:
                        print("HUE_GPU_RELOAD_UNAVAILABLE: configuration checked, GPU reload not exercised")
                    assert command(sock,"quit")["ok"]
                assert proc.wait(timeout=20) == 0
            except BaseException:
                if proc.poll() is None: proc.kill()
                proc.wait(timeout=10)
                log.seek(0)
                print(log.read().decode(errors="replace"), file=sys.stderr)
                raise
    print("configuration and shader reload smoke passed")

if __name__ == "__main__": main()
