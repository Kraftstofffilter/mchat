#!/usr/bin/env python3
# mchat-preview.py
#
# Shows an image full-screen in the terminal and waits for a key or mouse
# click. Uses the Kitty graphics protocol when the terminal answers its
# query (Kitty, Ghostty, WezTerm, also through herdr), otherwise draws the
# image with truecolor half-block characters. Needs Pillow.
#
# Usage: mchat-preview.py <image> [--blocks]
#
# As mchat's attachment_open_command (ui.conf), clicking a picture shows it:
#   attachment_open_command=~/Workspace/mchat/utils/mchat/mchat-preview.py '%1'
# Files that are not pictures exit at once without output.

import base64
import io
import os
import select
import shutil
import sys
import termios
import tty

from PIL import Image, ImageOps


def read_reply(fd, timeout):
    data = b""
    while True:
        ready, _, _ = select.select([fd], [], [], timeout)
        if not ready:
            return data
        data += os.read(fd, 1024)
        # the primary device attributes reply ends the probe
        if b"\x1b[?" in data and data.rstrip().endswith(b"c"):
            return data


def kitty_supported(fd):
    # graphics query plus a DA1 request: terminals without Kitty graphics
    # answer only the DA1
    sys.stdout.write("\x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\\x1b[c")
    sys.stdout.flush()
    return b"_Gi=31;OK" in read_reply(fd, 1.0)


def show_kitty(img, cols, rows):
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    data = base64.standard_b64encode(buf.getvalue()).decode("ascii")
    chunks = [data[i:i + 4096] for i in range(0, len(data), 4096)]
    out = sys.stdout
    for i, chunk in enumerate(chunks):
        more = 1 if i < len(chunks) - 1 else 0
        if i == 0:
            out.write(f"\x1b_Gf=100,a=T,q=2,c={cols},r={rows},m={more};{chunk}\x1b\\")
        else:
            out.write(f"\x1b_Gm={more};{chunk}\x1b\\")
    out.flush()


def show_blocks(img, cols, rows):
    # each cell shows two vertical pixels: upper as foreground of "▀",
    # lower as background
    img = img.convert("RGB")
    img.thumbnail((cols, rows * 2))
    w, h = img.size
    px = img.load()
    out = []
    for y in range(0, h - 1, 2):
        line = []
        for x in range(w):
            r1, g1, b1 = px[x, y]
            r2, g2, b2 = px[x, y + 1]
            line.append(f"\x1b[38;2;{r1};{g1};{b1}m\x1b[48;2;{r2};{g2};{b2}m▀")
        out.append("".join(line) + "\x1b[0m")
    sys.stdout.write("\r\n".join(out))
    sys.stdout.flush()


def main():
    if len(sys.argv) < 2:
        print("usage: mchat-preview.py <image> [--blocks]", file=sys.stderr)
        return 1

    path = sys.argv[1]
    force_blocks = "--blocks" in sys.argv[2:]
    cols, rows = shutil.get_terminal_size((80, 24))
    rows = max(rows - 1, 1)  # last row for the hint

    try:
        img = ImageOps.exif_transpose(Image.open(path))
    except Exception:
        # not a picture (document, audio, ...): nothing to show; mchat shows
        # its link instead
        return 0

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        sys.stdout.write("\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H")  # alt screen, hide cursor
        sys.stdout.write("\x1b[?1000h\x1b[?1006h")  # report clicks, to close on click
        use_kitty = (not force_blocks) and kitty_supported(fd)
        sys.stdout.write("\x1b[H")

        if use_kitty:
            # keep the aspect ratio within cols x rows cells (cells are ~2:1)
            w, h = img.size
            scale = min(cols / w, (rows * 2) / h)
            c = max(1, int(w * scale))
            r = max(1, int(h * scale / 2))
            img.thumbnail((1600, 1600))
            show_kitty(img.convert("RGBA"), c, r)
        else:
            show_blocks(img, cols, rows)

        name = os.path.basename(path)
        hint = f" {name}  -  any key or click to close "
        sys.stdout.write(f"\x1b[{rows + 1};1H\x1b[7m{hint[:cols]}\x1b[0m")
        sys.stdout.flush()

        os.read(fd, 1024)
    finally:
        sys.stdout.write("\x1b_Ga=d\x1b\\")  # delete images
        sys.stdout.write("\x1b[?1006l\x1b[?1000l\x1b[?25h\x1b[?1049l")
        sys.stdout.flush()
        termios.tcsetattr(fd, termios.TCSADRAIN, old)

    return 0


if __name__ == "__main__":
    sys.exit(main())
