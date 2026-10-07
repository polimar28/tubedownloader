"""Generates resources/app.ico (a simple original download-arrow icon)."""
from PIL import Image, ImageDraw

S = 256
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
d.rounded_rectangle((8, 8, S - 8, S - 8), radius=56, fill=(37, 99, 235, 255))
# Arrow shaft + head
d.rounded_rectangle((110, 52, 146, 150), radius=10, fill="white")
d.polygon([(70, 128), (186, 128), (128, 192)], fill="white")
# Tray
d.rounded_rectangle((64, 202, 192, 222), radius=10, fill="white")
img.save("resources/app.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("ok")
