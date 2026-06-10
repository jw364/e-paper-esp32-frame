import tkinter as tk
import os
import json
from tkinter import filedialog as fd, messagebox
from PIL import ImageTk, Image

# ── E6 Spectra 6 colour palette ──────────────────────────────────────────────
# Order must match: index → EPD nibble code
_PALETTE_RGB = [
    (0,   0,   0  ),  # 0 → Black  0x0
    (255, 255, 255),  # 1 → White  0x1
    (255, 255, 0  ),  # 2 → Yellow 0x2
    (255, 0,   0  ),  # 3 → Red    0x3
    (0,   0,   255),  # 4 → Blue   0x5  (nibble 0x5, not 0x4)
    (0,   255, 0  ),  # 5 → Green  0x6
]
_EPD_CODES = [0x0, 0x1, 0x2, 0x3, 0x5, 0x6]

DISPLAY_W = 600
DISPLAY_H = 400
MAX_PER_ALBUM = 25


def _build_pil_palette() -> Image.Image:
    """Return a mode-P image whose palette covers our 6 E6 colours."""
    pal = Image.new("P", (1, 1))
    flat = []
    for r, g, b in _PALETTE_RGB:
        flat += [r, g, b]
    flat += [0] * (768 - len(flat))   # pad to 256 × 3 entries
    pal.putpalette(flat)
    return pal


_PIL_PALETTE = _build_pil_palette()


def _image_to_epd_bin(img: Image.Image) -> bytes:
    """
    Resize `img` to 600×400, apply Floyd-Steinberg dithering against the
    Spectra 6 palette, and return the packed 4-bit binary (120 000 bytes).
    Two pixels are packed per byte: high nibble = left pixel, low nibble = right.
    """
    img = img.convert("RGB").resize((DISPLAY_W, DISPLAY_H), Image.LANCZOS)

    # PIL's C-implemented dither is fast and correct for this palette
    quantized = img.quantize(palette=_PIL_PALETTE,
                             dither=Image.Dither.FLOYDSTEINBERG)

    pixels = list(quantized.getdata())   # palette indices 0-5

    result = bytearray()
    for i in range(0, len(pixels) - 1, 2):
        hi = _EPD_CODES[pixels[i]]
        lo = _EPD_CODES[pixels[i + 1]]
        result.append((hi << 4) | lo)
    # If total pixel count is odd (shouldn't happen for 600×400) pad with white
    if len(pixels) % 2:
        result.append((_EPD_CODES[pixels[-1]] << 4) | 0x1)

    return bytes(result)


class ImageHandler:
    main = None
    imageSelected = None

    def __init__(self, main):
        self.main = main
        # Each entry:
        #   { filename, original_filepath, x, y, x_offset, y_offset,
        #     rotate, scale, album }
        # album = 0, 1, or None (unassigned)
        self.fileData = []

    # ── Loading ──────────────────────────────────────────────────────────────
    def loadImages(self):
        paths = fd.askopenfilenames(
            title="Open image files",
            initialdir="/",
            filetypes=[("Images", "*.jpg *.jpeg *.png *.bmp")])
        if not paths:
            return

        relative_dir = os.path.dirname(paths[0])
        backup_path  = os.path.join(relative_dir, "backup.json")
        saved = []
        if not self.fileData and os.path.exists(backup_path):
            with open(backup_path, "r") as f:
                saved = json.load(f)

        for path in paths:
            basename = os.path.basename(path)
            found = next((s for s in saved if s.get("filename") == basename), None)
            if found:
                found["original_filepath"] = path
                self.fileData.append(found)
            else:
                self._initImage(path)

            self._refreshListEntry(len(self.fileData) - 1)

        self.imageSelected = 0
        self.main.listbox.selection_set(0)
        self.canvasImage(0)
        self._updateAlbumCounts()

    def _initImage(self, filepath):
        self.fileData.append({
            "filename":          os.path.basename(filepath),
            "original_filepath": filepath,
            "x": 0, "y": 0,
            "x_offset": 0, "y_offset": 0,
            "rotate": 0, "scale": 1,
            "album": None,
        })
        self._setImageSize(len(self.fileData) - 1)

    # ── Album assignment ─────────────────────────────────────────────────────
    def setImageAlbum(self, album: int):
        if self.imageSelected is None:
            return
        # Check slot availability
        count = sum(1 for d in self.fileData if d["album"] == album)
        current = self.fileData[self.imageSelected]["album"]
        if current != album and count >= MAX_PER_ALBUM:
            messagebox.showwarning(
                "Album full",
                f"Album {album} already has {MAX_PER_ALBUM} photos.",
                parent=self.main.root)
            return
        self.fileData[self.imageSelected]["album"] = album
        self._refreshListEntry(self.imageSelected)
        self._saveBackup(self.imageSelected)
        self._updateAlbumCounts()

    def clearImageAlbum(self):
        if self.imageSelected is None:
            return
        self.fileData[self.imageSelected]["album"] = None
        self._refreshListEntry(self.imageSelected)
        self._saveBackup(self.imageSelected)
        self._updateAlbumCounts()

    def _updateAlbumCounts(self):
        c0 = sum(1 for d in self.fileData if d["album"] == 0)
        c1 = sum(1 for d in self.fileData if d["album"] == 1)
        self.main.album0_count_var.set(f"Album 0: {c0}/{MAX_PER_ALBUM}")
        self.main.album1_count_var.set(f"Album 1: {c1}/{MAX_PER_ALBUM}")

    def _refreshListEntry(self, index):
        d = self.fileData[index]
        name = os.path.basename(d["original_filepath"])
        if d["album"] is not None:
            label = f"[Album {d['album']}] {name}"
            bg    = '#a8d8a8' if d["album"] == 0 else '#f9c784'
        else:
            label = name
            bg    = 'white'
        self.main.listbox.delete(index)
        self.main.listbox.insert(index, label)
        self.main.listbox.itemconfig(index, {'bg': bg})
        self.main.listbox.selection_set(index)

    # ── Export ───────────────────────────────────────────────────────────────
    def exportImages(self):
        if not self.fileData:
            messagebox.showinfo("Nothing to export", "No images loaded.",
                                parent=self.main.root)
            return

        out_dir = fd.askdirectory(title="Select output directory (data/ will be created here)",
                                  initialdir="/")
        if not out_dir:
            return

        a0 = [d for d in self.fileData if d["album"] == 0]
        a1 = [d for d in self.fileData if d["album"] == 1]

        if not a0 and not a1:
            messagebox.showinfo("No albums assigned",
                                "Assign photos to Album 0 or Album 1 before exporting.",
                                parent=self.main.root)
            return

        errors = []
        for album_idx, album_data in ((0, a0), (1, a1)):
            album_dir = os.path.join(out_dir, "data", f"a{album_idx}")
            os.makedirs(album_dir, exist_ok=True)
            for photo_idx, entry in enumerate(album_data[:MAX_PER_ALBUM]):
                try:
                    img = self._getAdaptedImage(entry)
                    if img.mode == "RGBA":
                        bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
                        img = Image.alpha_composite(bg, img)
                    if img.mode != "RGB":
                        img = img.convert("RGB")

                    # Paste onto exact 600×400 canvas with white background
                    canvas = Image.new("RGB", (DISPLAY_W, DISPLAY_H), (255, 255, 255))
                    canvas.paste(img, (int(-entry["x_offset"]),
                                      int(-entry["y_offset"])))

                    bin_data = _image_to_epd_bin(canvas)
                    out_path = os.path.join(album_dir, f"{photo_idx:02d}.bin")
                    with open(out_path, "wb") as f:
                        f.write(bin_data)
                except Exception as e:
                    errors.append(f"{entry['filename']}: {e}")

        if errors:
            messagebox.showwarning("Export finished with errors",
                                   "\n".join(errors), parent=self.main.root)
        else:
            a0_count = min(len(a0), MAX_PER_ALBUM)
            a1_count = min(len(a1), MAX_PER_ALBUM)
            messagebox.showinfo(
                "Export complete",
                f"Exported {a0_count} photo(s) to data/a0/\n"
                f"Exported {a1_count} photo(s) to data/a1/\n\n"
                f"Upload the data/ folder to the ESP32 using the\n"
                f"Arduino IDE LittleFS Data Uploader plug-in.",
                parent=self.main.root)

    # ── Delete ───────────────────────────────────────────────────────────────
    def deleteImage(self):
        if self.imageSelected is None:
            return
        self.main.listbox.delete(self.imageSelected)
        self.fileData.pop(self.imageSelected)
        if not self.fileData:
            self.imageSelected = None
            self.main.canvas.delete("all")
        else:
            self.imageSelected = min(self.imageSelected, len(self.fileData) - 1)
            self.main.listbox.selection_set(self.imageSelected)
            self.canvasImage(self.imageSelected)
        self._updateAlbumCounts()

    def deleteAllImages(self):
        self.main.listbox.delete(0, tk.END)
        self.fileData.clear()
        self.imageSelected = None
        self.main.canvas.delete("all")
        self._updateAlbumCounts()

    # ── Image manipulation ───────────────────────────────────────────────────
    def resetImage(self, index):
        if index is None:
            return
        self.fileData[index].update(x_offset=0, y_offset=0, scale=1, rotate=0)
        self._setImageSize(index)
        self.canvasImage(index)
        self._saveBackup(index)

    def rotateImage(self, angle):
        if self.imageSelected is None:
            return
        self.fileData[self.imageSelected]["rotate"] += angle
        self.fileData[self.imageSelected]["scale"] = 1
        self._setImageSize(self.imageSelected)
        self._saveBackup(self.imageSelected)
        self.canvasImage(self.imageSelected)

    def changeOffset(self, x, y):
        if self.imageSelected is None:
            return
        self.fileData[self.imageSelected]["x_offset"] += x
        self.fileData[self.imageSelected]["y_offset"] += y
        self._saveBackup(self.imageSelected)
        self.canvasImage(self.imageSelected)

    def changeScale(self, value):
        if self.imageSelected is None:
            return
        d = self.fileData[self.imageSelected]
        new_scale = d["scale"] + value
        if new_scale <= 0.05:
            return
        d["scale"] = new_scale
        d["x_offset"] += d["x"] / 2 * value
        d["y_offset"] += d["y"] / 2 * value
        self._saveBackup(self.imageSelected)
        self.canvasImage(self.imageSelected)

    # ── Canvas preview ───────────────────────────────────────────────────────
    def canvasImage(self, index):
        img   = self._getAdaptedImage(self.fileData[index])
        photo = ImageTk.PhotoImage(img)
        d     = self.fileData[index]

        self.main.canvas.image = photo
        self.main.canvas.create_image(
            -d["x_offset"] + self.main.offsetFrameX,
            -d["y_offset"] + self.main.offsetFrameY,
            image=photo, anchor=tk.NW)

        x2 = self.main.offsetFrameX + DISPLAY_W
        y2 = self.main.offsetFrameY + DISPLAY_H
        self.main.canvas.create_rectangle(
            self.main.offsetFrameX, self.main.offsetFrameY, x2, y2,
            outline='red', width=3, dash=(4, 4))

    # ── Helpers ──────────────────────────────────────────────────────────────
    def _setImageSize(self, index):
        d   = self.fileData[index]
        img = Image.open(d["original_filepath"])
        rotated_90 = d["rotate"] % 180 != 0

        if rotated_90:
            aspect = img.width / img.height
            new_h = DISPLAY_W
            new_w = round(new_h * aspect)
            if new_w < DISPLAY_H:
                new_w = DISPLAY_H
                new_h = round(new_w / aspect)
                x_off = (new_h - DISPLAY_W) / 2
                y_off = 0
            else:
                x_off = 0
                y_off = (new_w - DISPLAY_H) / 2
        else:
            aspect = img.width / img.height
            new_w = DISPLAY_W
            new_h = round(new_w / aspect)
            if new_h < DISPLAY_H:
                new_h = DISPLAY_H
                new_w = round(new_h * aspect)
                x_off = (new_w - DISPLAY_W) / 2
                y_off = 0
            else:
                x_off = 0
                y_off = (new_h - DISPLAY_H) / 2

        d.update(x=new_w, y=new_h, x_offset=x_off, y_offset=y_off)

    def _getAdaptedImage(self, d):
        img = Image.open(d["original_filepath"])
        img = img.resize((int(d["x"] * d["scale"]), int(d["y"] * d["scale"])))
        img = img.rotate(d["rotate"], expand=True)
        return img

    def _saveBackup(self, index):
        src_dir = os.path.dirname(self.fileData[index]["original_filepath"])
        backup_path = os.path.join(src_dir, "backup.json")
        with open(backup_path, "w") as f:
            json.dump(self.fileData, f)
