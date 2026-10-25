import sys, glob
from PIL import Image
files = sorted(glob.glob(sys.argv[1] + "/*.ppm"))[: int(sys.argv[3]) if len(sys.argv) > 3 else 12]
ims = [Image.open(f) for f in files]
cols = 4; rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * 324, rows * 240), (40, 40, 40))
from PIL import ImageDraw
d = ImageDraw.Draw(sheet)
for i, (f, im) in enumerate(zip(files, ims)):
    x, y = (i % cols) * 324, (i // cols) * 240
    sheet.paste(im, (x + 2, y + 14)); d.text((x + 4, y + 1), f.split('/')[-1], fill=(255, 255, 0))
sheet.save(sys.argv[2])
