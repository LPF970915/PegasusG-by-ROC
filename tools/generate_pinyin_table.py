"""Generate the offline table from mozillazg/pinyin-data v0.13.0 pinyin.txt.

Usage: python tools/generate_pinyin_table.py /path/to/pinyin.txt
The build uses the committed output and needs neither Python nor a network.
"""
import pathlib
import sys
import unicodedata

entries = []
for line in pathlib.Path(sys.argv[1]).read_text(encoding="utf-8").splitlines():
    if not line.startswith("U+"):
        continue
    code, values = line.split(":", 1)
    reading = values.split("#", 1)[0].split(",", 1)[0].strip()
    # Keep umlaut as keyboard 'v', removing only tone marks.
    decomposed = unicodedata.normalize("NFD", reading)
    reading = decomposed.replace("u\u0308", "v")
    reading = "".join(c for c in reading if not unicodedata.combining(c))
    entries.append((int(code[2:], 16), reading))
output = pathlib.Path(__file__).resolve().parents[1] / "src/pinyin_table.inc"
rows = [f'{{0x{code:X}, "{reading}"}},' for code, reading in sorted(entries)]
output.write_text(
    "// Generated from pinyin-data v0.13.0; see third_party/licenses/pinyin-data-LICENSE.\n"
    + "\n".join(" ".join(rows[i:i + 4]) for i in range(0, len(rows), 4)) + "\n",
    encoding="utf-8",
    newline="\n",
)
