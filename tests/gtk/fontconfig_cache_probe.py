"""Check that a patched heap representation preserves cache interoperability."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from xml.sax.saxutils import escape

library = Path(sys.argv[1])
font = next(Path("/usr/share/fonts").rglob("*.ttf"))
with tempfile.TemporaryDirectory(prefix="tarn-fontconfig-", dir="/tmp") as temporary:
    root = Path(temporary)
    fonts, cache = root / "fonts", root / "cache"
    fonts.mkdir()
    cache.mkdir()
    shutil.copyfile(font, fonts / "sample.ttf")
    config = root / "fonts.conf"
    config.write_text(
        '<fontconfig><dir>' + escape(str(fonts)) + '</dir><cachedir>'
        + escape(str(cache)) + '</cachedir></fontconfig>'
    )
    environment = dict(os.environ, FONTCONFIG_FILE=str(config))
    environment.pop("LD_LIBRARY_PATH", None)
    patched = dict(environment, LD_LIBRARY_PATH=str(library))

    def run(command, env):
        return subprocess.run(command, env=env, check=True, capture_output=True,
                              text=True, timeout=30).stdout

    def digest():
        return {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                for path in cache.glob("*.cache-*")}

    run([str(library / "fc-cache/fc-cache"), "-f", str(fonts)], patched)
    before = digest()
    assert before, "patched library did not create a font cache"
    format_argument = "--format=%{family}\n"
    local = run([str(library / "fc-list/fc-list"), format_argument], patched)
    system = run(["fc-list", format_argument], environment)
    assert local.strip() and local == system, "cache readers disagree"
    assert before == digest(), "reading unexpectedly rewrote the serialized cache"
    # Exercise the opposite direction with a cache produced by the system library.
    run(["fc-cache", "-f", str(fonts)], environment)
    reverse = digest()
    reread = run([str(library / "fc-list/fc-list"), format_argument], patched)
    assert reread == system and reverse == digest(), "reverse roundtrip failed"
    print(json.dumps({"cache_roundtrip": "passed", "font": font.name,
                      "family": local.strip(), "cache_files": len(before)}))
