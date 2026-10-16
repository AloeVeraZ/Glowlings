"""Turn animation_catalog.cpp frames into labeled GIF sheets of real firmware."""

from pathlib import Path

import argparse

from PIL import Image, ImageDraw, ImageFont, ImageChops



parser=argparse.ArgumentParser()

parser.add_argument("frames",type=Path)

parser.add_argument("output",type=Path)

args=parser.parse_args()

args.output.mkdir(parents=True,exist_ok=True)

font_path=Path("C:/Windows/Fonts/segoeui.ttf")

font=ImageFont.truetype(str(font_path),12) if font_path.exists() else ImageFont.load_default()

title_font=ImageFont.truetype(str(font_path),18) if font_path.exists() else font

pages=[("01-expressions","15 base expressions"),("02-behaviors","Behaviors 1–16"),("03-behaviors","Behaviors 17–31"),("04-effects","16 effects and wake variants"),("05-blinks-naps","Blink variants and optional nap peeks")]

for page,(name,title) in enumerate(pages):

    labels=[line.split("\t",1)[1] for line in (args.frames/f"page{page}.tsv").read_text().splitlines()]

    rgb=[]

    for source in sorted(args.frames.glob(f"page{page}_*.ppm")):

        raw=Image.open(source).convert("RGB")

        canvas=Image.new("RGB",(raw.width,raw.height+44),(15,16,22))

        canvas.paste(raw,(0,44))

        d=ImageDraw.Draw(canvas)

        d.text((12,7),title,font=title_font,fill=(234,230,250))

        d.text((12,29),"Star · real firmware frames · 6 second loop",font=font,fill=(150,145,165))

        for i,label in enumerate(labels):

            x=i%4*144;y=i//4*172+44+144

            d.rectangle((x,y,x+143,y+27),fill=(19,19,25))

            text=f"{i+1 if page!=2 else i+17}. {label}"

            while d.textbbox((0,0),text,font=font)[2]>140 and ' / ' in text:

                text=text.replace(' / ','/',1)

            d.text((x+4,y+5),text,font=font,fill=(210,204,225))

        rgb.append(canvas)

    palette_source=Image.new("RGB",(rgb[0].width*4,rgb[0].height))

    for i,n in enumerate([0,18,38,58]):palette_source.paste(rgb[n],(i*rgb[0].width,0))

    palette=palette_source.quantize(colors=256,method=Image.Quantize.MEDIANCUT)

    frames=[im.quantize(palette=palette,dither=Image.Dither.NONE) for im in rgb]

    path=args.output/f"{name}.gif"

    frames[0].save(path,save_all=True,append_images=frames[1:],duration=80,loop=0,disposal=1,optimize=True)

    with Image.open(path) as check:
        assert check.n_frames==len(frames)
        for n in [0,18,38,58,74]:
            check.seek(n)
            assert ImageChops.difference(check.convert("RGB"),frames[n].convert("RGB")).getbbox() is None, (name,n)
    rgb[18].save(args.output/f"{name}-check.png")

    print(f"{path.name}: {len(labels)} tiles, {len(frames)} frames, {path.stat().st_size:,} bytes",flush=True)
