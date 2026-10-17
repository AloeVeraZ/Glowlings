"""Turn highlights.cpp frames into labeled GIFs, styled like catalog.py."""

from pathlib import Path

import argparse

from PIL import Image, ImageDraw, ImageFont, ImageChops

parser = argparse.ArgumentParser()
parser.add_argument("frames", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)

font_path = Path("C:/Windows/Fonts/segoeui.ttf")
font = ImageFont.truetype(str(font_path), 12) if font_path.exists() else ImageFont.load_default()
title_font = ImageFont.truetype(str(font_path), 18) if font_path.exists() else font

scenes = [("starfall", "07-always-on-starfall", "Always-on: stars falling from the pupils"),
          ("double-tap", "08-double-tap-zero", "Double-tap zero"),
          ("battery", "09-battery-full", "Battery readout: 95-100% reads 100%")]
LABEL_H = 28
measure = ImageDraw.Draw(Image.new("RGB", (1, 1)))


def wrap(text, width):
    lines = [""]
    for word in text.split():
        trial = f"{lines[-1]} {word}".strip()
        if measure.textbbox((0, 0), trial, font=font)[2] > width and lines[-1]:
            lines.append(word)
        else:
            lines[-1] = trial
    return lines


for source, name, title in scenes:
    labels = [line.split("\t") for line in (args.frames / f"{source}.tsv").read_text().splitlines()]
    captions = (args.frames / f"{source}_captions.txt").read_text().splitlines()
    sources = sorted(args.frames.glob(f"{source}_[0-9][0-9][0-9].ppm"))
    width = Image.open(sources[0]).width
    wrapped = [wrap(caption, width - 24) for caption in captions]
    HEADER = 30 + 16 * max(len(lines) for lines in wrapped)
    rgb = []
    for n, path in enumerate(sources):
        raw = Image.open(path).convert("RGB")
        canvas = Image.new("RGB", (raw.width, raw.height + HEADER), (15, 16, 22))
        canvas.paste(raw, (0, HEADER))
        d = ImageDraw.Draw(canvas)
        d.text((12, 7), title, font=title_font, fill=(234, 230, 250))
        for i, line in enumerate(wrapped[n]):
            d.text((12, 29 + 16 * i), line, font=font, fill=(150, 145, 165))
        for x, y, w, label in labels:
            x, w = int(x), int(w)
            top = HEADER + raw.height - LABEL_H
            d.rectangle((x, top, x + w - 1, top + LABEL_H - 1), fill=(19, 19, 25))
            d.text((x + 6, top + 6), label, font=font, fill=(210, 204, 225))
        rgb.append(canvas)
    picks = [0, len(rgb) // 4, len(rgb) // 2, 3 * len(rgb) // 4]
    palette_source = Image.new("RGB", (rgb[0].width * len(picks), rgb[0].height))
    for i, n in enumerate(picks):
        palette_source.paste(rgb[n], (i * rgb[0].width, 0))
    palette = palette_source.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    frames = [im.quantize(palette=palette, dither=Image.Dither.NONE) for im in rgb]
    out = args.output / f"{name}.gif"
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=80, loop=0, disposal=1, optimize=True)
    # Identical consecutive frames (a held readout) are merged, so check the
    # first frame and the total loop time rather than the frame count.
    with Image.open(out) as check:
        assert ImageChops.difference(check.convert("RGB"), frames[0].convert("RGB")).getbbox() is None, name
        total = 0
        for n in range(check.n_frames):
            check.seek(n)
            total += check.info["duration"]
        assert total == 80 * len(frames), (name, total)
    print(f"{out.name}: {len(frames)} frames, {out.stat().st_size:,} bytes", flush=True)
