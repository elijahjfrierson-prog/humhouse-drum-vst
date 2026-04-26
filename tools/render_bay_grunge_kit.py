#!/usr/bin/env python3
"""rc.11 — generate the (Bay Grunge) Yamaha Maple kit illustration.

The Nu Rock kit ships a 1536x1024 photo-style render
(`Resources/Branding/NuRockYamahaKit.png`). This script produces a
matching-resolution stylized 3D-looking render for the second bundled
kit "(Bay Grunge) Yamaha Maple" — warm honey/amber maple shells, brass
hi-hat, two crash cymbals splayed left/right, kick + snare + rack/floor
toms — drawn programmatically with PIL so we don't need an external
asset and the build is reproducible.

Output: Resources/Branding/BayGrungeMapleKit.png
"""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

W, H = 1536, 1024
OUT = Path(__file__).resolve().parents[1] / "Resources" / "Branding" / "BayGrungeMapleKit.png"


def lerp(a: tuple[int, int, int], b: tuple[int, int, int], t: float) -> tuple[int, int, int]:
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


# --- Palette ---------------------------------------------------------------
BG_TOP    = (28, 22, 26)     # deep wine-black
BG_BOT    = (74, 50, 40)     # smoked maple-floor
SHELL_HI  = (242, 187, 116)  # honey-maple highlight
SHELL_MID = (190, 128, 70)   # warm amber wood
SHELL_LO  = (98, 56, 30)     # cocoa shadow
HOOP      = (210, 175, 110)  # brushed brass hoops
HOOP_DARK = (110, 88, 50)
HEAD      = (236, 228, 210)  # vintage aged drumhead
HEAD_DARK = (175, 165, 145)
CYMBAL_HI = (224, 190, 110)
CYMBAL_LO = (134, 102, 50)
HW_CHROME = (170, 168, 165)
HW_DARK   = (60, 60, 64)


def vertical_gradient(img: Image.Image, top, bot) -> None:
    px = img.load()
    for y in range(img.height):
        t = y / max(1, img.height - 1)
        c = lerp(top, bot, t)
        for x in range(img.width):
            px[x, y] = c


def filled_ellipse(d: ImageDraw.ImageDraw, bbox, color):
    d.ellipse(bbox, fill=color)


def shell(d: ImageDraw.ImageDraw, cx, cy, w, h, depth):
    """Cylindrical maple drum shell — body + hoops + lugs."""
    # Body wood band — vertical strip with horizontal grain shading.
    body_top = cy - h // 2
    body_bot = cy + h // 2
    for i in range(int(w)):
        t = abs((i - w / 2) / (w / 2))           # 0 center → 1 edge
        wood = lerp(SHELL_HI, SHELL_MID, t)
        wood = lerp(wood, SHELL_LO, t * t * 0.6)
        d.line([(cx - w // 2 + i, body_top), (cx - w // 2 + i, body_bot)],
               fill=wood, width=1)

    # Soft horizontal grain striations.
    for k in range(8):
        ystr = body_top + int(h * (k + 0.5) / 8)
        d.line([(cx - w // 2 + 4, ystr), (cx + w // 2 - 4, ystr)],
               fill=lerp(SHELL_LO, SHELL_HI, 0.25), width=1)

    # Top + bottom hoops.
    hoop_h = max(8, h // 14)
    d.rectangle([cx - w // 2 - 4, body_top - hoop_h, cx + w // 2 + 4, body_top],
                fill=HOOP, outline=HOOP_DARK, width=2)
    d.rectangle([cx - w // 2 - 4, body_bot, cx + w // 2 + 4, body_bot + hoop_h],
                fill=HOOP, outline=HOOP_DARK, width=2)

    # Tension rod lugs.
    for n in range(6):
        lugx = cx - w // 2 + int((n + 0.5) * w / 6)
        d.rectangle([lugx - 4, body_top + 6, lugx + 4, body_bot - 6],
                    fill=HOOP_DARK)
        d.line([(lugx, body_top - hoop_h - 2), (lugx, body_top - 2)],
               fill=HW_CHROME, width=2)
        d.line([(lugx, body_bot + 2), (lugx, body_bot + hoop_h + 2)],
               fill=HW_CHROME, width=2)


def drumhead_top(d: ImageDraw.ImageDraw, cx, cy, w, depth):
    """Top circular head, painted as an ellipse seen from above."""
    e = depth
    bbox = [cx - w // 2, cy - e, cx + w // 2, cy + e]
    filled_ellipse(d, bbox, HEAD)
    d.ellipse(bbox, outline=HEAD_DARK, width=3)
    inner = [cx - w // 2 + 6, cy - e + 4, cx + w // 2 - 6, cy + e - 4]
    d.ellipse(inner, outline=lerp(HEAD, HEAD_DARK, 0.5), width=1)
    # central stick mark / faint logo wash
    d.ellipse([cx - 18, cy - 6, cx + 18, cy + 6],
              fill=lerp(HEAD, HEAD_DARK, 0.25))


def cymbal(d: ImageDraw.ImageDraw, cx, cy, rx, ry, tilt=0.0):
    """Tilted cymbal as a slim ellipse with brass gradient + ride grooves."""
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    # outer disc
    ld.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=(*CYMBAL_HI, 255))
    ld.ellipse([cx - rx + 2, cy - ry + 1, cx + rx - 2, cy + ry - 1],
               outline=(*CYMBAL_LO, 220), width=2)
    # concentric ride grooves
    for k in range(6):
        f = 0.92 - k * 0.12
        ld.ellipse([cx - rx * f, cy - ry * f, cx + rx * f, cy + ry * f],
                   outline=(*lerp(CYMBAL_HI, CYMBAL_LO, 0.45), 200), width=1)
    # bell
    bell_rx = max(8, rx // 6)
    bell_ry = max(4, ry // 6)
    ld.ellipse([cx - bell_rx, cy - bell_ry, cx + bell_rx, cy + bell_ry],
               fill=(*lerp(CYMBAL_HI, (255, 230, 170), 0.4), 255),
               outline=(*CYMBAL_LO, 220), width=2)

    if abs(tilt) > 1e-3:
        layer = layer.rotate(math.degrees(tilt), resample=Image.BICUBIC, center=(cx, cy))
    return layer


def hardware_stand(d: ImageDraw.ImageDraw, x, y_bottom, y_top):
    d.line([(x, y_bottom), (x, y_top)], fill=HW_CHROME, width=4)
    d.polygon([(x - 30, y_bottom), (x + 30, y_bottom),
               (x + 18, y_bottom - 14), (x - 18, y_bottom - 14)],
              fill=HW_DARK, outline=HW_CHROME)


def main() -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)

    img = Image.new("RGB", (W, H), BG_TOP)
    vertical_gradient(img, BG_TOP, BG_BOT)

    # Stage rim shadow
    shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    sd.ellipse([W // 2 - 700, H - 280, W // 2 + 700, H - 60],
               fill=(0, 0, 0, 110))
    shadow = shadow.filter(ImageFilter.GaussianBlur(40))
    img = Image.alpha_composite(img.convert("RGBA"), shadow).convert("RGB")

    d = ImageDraw.Draw(img)

    # --- Kick (huge front-and-center) ---
    kick_cx, kick_cy = W // 2, int(H * 0.66)
    shell(d, kick_cx, kick_cy, w=560, h=380, depth=160)
    drumhead_top(d, kick_cx, kick_cy - 190, w=560, depth=140)

    # --- Floor tom (right) ---
    ft_cx, ft_cy = int(W * 0.78), int(H * 0.62)
    shell(d, ft_cx, ft_cy, w=300, h=300, depth=110)
    drumhead_top(d, ft_cx, ft_cy - 150, w=300, depth=80)

    # --- Snare (left of kick, lower) ---
    sn_cx, sn_cy = int(W * 0.36), int(H * 0.58)
    shell(d, sn_cx, sn_cy, w=220, h=110, depth=70)
    drumhead_top(d, sn_cx, sn_cy - 55, w=220, depth=50)

    # --- Rack tom (above kick, slight left tilt) ---
    rt_cx, rt_cy = int(W * 0.52), int(H * 0.36)
    shell(d, rt_cx, rt_cy, w=200, h=180, depth=70)
    drumhead_top(d, rt_cx, rt_cy - 90, w=200, depth=55)

    # --- Stands behind ---
    hardware_stand(d, int(W * 0.21), int(H * 0.92), int(H * 0.40))  # hat
    hardware_stand(d, int(W * 0.18), int(H * 0.92), int(H * 0.35))  # left crash
    hardware_stand(d, int(W * 0.86), int(H * 0.92), int(H * 0.30))  # right crash

    # --- Cymbals (composited last so they sit on top) ---
    img = img.convert("RGBA")

    # Hi-hat (low, slight tilt)
    img = Image.alpha_composite(
        img, cymbal(ImageDraw.Draw(img), int(W * 0.21), int(H * 0.39),
                    rx=140, ry=22, tilt=-0.08))
    # Left crash (medium height, opens right)
    img = Image.alpha_composite(
        img, cymbal(ImageDraw.Draw(img), int(W * 0.16), int(H * 0.30),
                    rx=170, ry=26, tilt=0.18))
    # Right crash (taller, opens right)
    img = Image.alpha_composite(
        img, cymbal(ImageDraw.Draw(img), int(W * 0.88), int(H * 0.26),
                    rx=185, ry=28, tilt=-0.14))

    # Bay Grunge / wax-paper warmth wash
    overlay = Image.new("RGBA", img.size, (255, 200, 130, 14))
    img = Image.alpha_composite(img, overlay).convert("RGB")

    # Soft vignette
    vmask = Image.new("L", img.size, 0)
    vd = ImageDraw.Draw(vmask)
    vd.rectangle([0, 0, W, H], fill=255)
    vd.ellipse([-180, -180, W + 180, H + 180], fill=80)
    vmask = vmask.filter(ImageFilter.GaussianBlur(180))
    dark = Image.new("RGB", img.size, (10, 8, 6))
    img = Image.composite(dark, img, vmask)

    img.save(OUT, "PNG", optimize=True)
    print(f"wrote {OUT.relative_to(OUT.parents[2])} ({img.size[0]}x{img.size[1]})")


if __name__ == "__main__":
    main()
