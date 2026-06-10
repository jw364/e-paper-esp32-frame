import tkinter as tk
from tkinter import filedialog as fd
from tkinter import Canvas
from PIL import ImageTk, Image
from imageHandler import ImageHandler

class ImageApp:
    def __init__(self):
        self.imageHandler = ImageHandler(self)
        self.up_pressed = False
        self.down_pressed = False
        self.left_pressed = False
        self.right_pressed = False
        self.plus_pressed = False
        self.minus_pressed = False
        self.delete_pressed = False
        self.dot_pressed = False
        self.comma_pressed = False
        # Canvas outer dimensions
        self.outerFrameX = 750
        self.outerFrameY = 480
        # Display resolution
        self.DISPLAY_W = 600
        self.DISPLAY_H = 400
        # Canvas offset so the frame rectangle is centred
        self.offsetFrameX = (self.outerFrameX - self.DISPLAY_W) / 2
        self.offsetFrameY = (self.outerFrameY - self.DISPLAY_H) / 2

        # ── Main window ──────────────────────────────────────────────────────
        self.root = tk.Tk()
        self.root.configure(bg='#FFFDEE')
        self.root.title("E-Paper Album Converter — 600×400 Spectra 6")
        self.root.geometry("1050x530")
        self.root.minsize(300 + self.outerFrameX, self.outerFrameY + 30)
        self.root.after(20, self.update)
        self.root.bind_class("Button", "<Key-Return>", lambda e: e.widget.invoke())
        self.root.unbind_class("Button", "<Key-space>")
        self.root.bind_class("Listbox", "<Key-Return>",
                             lambda e: e.widget.event_generate("<<ListboxSelect>>"))

        # ── Left control panel ───────────────────────────────────────────────
        self.left_frame = tk.Frame(self.root, bg='#FFFDEE')
        for i in range(12):
            self.left_frame.grid_rowconfigure(i, weight=1)
        for i in range(2):
            self.left_frame.grid_columnconfigure(i, weight=1)
        self.left_frame.grid(row=0, column=0, sticky="nsew", padx=4, pady=4)

        row = 0
        tk.Button(self.left_frame, text="Load Images",
                  command=self.imageHandler.loadImages,
                  width=14, bg='#8fc9e8').grid(row=row, column=0, columnspan=2,
                                               sticky="nsew")
        row += 1
        self.listbox = tk.Listbox(self.left_frame, selectmode=tk.SINGLE, height=16)
        self.listbox.grid(row=row, column=0, columnspan=2, sticky="nsew")
        self.listbox.bind('<<ListboxSelect>>', self.on_selection_change)
        row += 1

        tk.Button(self.left_frame, text="Delete",
                  command=self.imageHandler.deleteImage,
                  bg='#8fc9e8').grid(row=row, column=0, sticky="nsew")
        tk.Button(self.left_frame, text="Delete All",
                  command=self.imageHandler.deleteAllImages,
                  bg='#8fc9e8').grid(row=row, column=1, sticky="nsew")
        row += 1

        # ── Album assignment ─────────────────────────────────────────────────
        self.album_label = tk.Label(self.left_frame, text="Assign selected image to:",
                                    bg='#FFFDEE')
        self.album_label.grid(row=row, column=0, columnspan=2, sticky="nsew")
        row += 1

        self.btn_album0 = tk.Button(self.left_frame, text="Album 0",
                                    command=lambda: self.imageHandler.setImageAlbum(0),
                                    bg='#a8d8a8')
        self.btn_album0.grid(row=row, column=0, sticky="nsew")
        self.btn_album1 = tk.Button(self.left_frame, text="Album 1",
                                    command=lambda: self.imageHandler.setImageAlbum(1),
                                    bg='#f9c784')
        self.btn_album1.grid(row=row, column=1, sticky="nsew")
        row += 1

        tk.Button(self.left_frame, text="Clear Album",
                  command=self.imageHandler.clearImageAlbum,
                  bg='#8fc9e8').grid(row=row, column=0, columnspan=2, sticky="nsew")
        row += 1

        # Album count labels
        self.album0_count_var = tk.StringVar(value="Album 0: 0/25")
        self.album1_count_var = tk.StringVar(value="Album 1: 0/25")
        tk.Label(self.left_frame, textvariable=self.album0_count_var,
                 bg='#a8d8a8').grid(row=row, column=0, sticky="nsew")
        tk.Label(self.left_frame, textvariable=self.album1_count_var,
                 bg='#f9c784').grid(row=row, column=1, sticky="nsew")
        row += 1

        # ── Scale / rotate / reset ────────────────────────────────────────────
        tk.Button(self.left_frame, text="-",
                  command=lambda: self.imageHandler.changeScale(-.1),
                  bg='#8fc9e8').grid(row=row, column=0, sticky="nsew")
        tk.Button(self.left_frame, text="+",
                  command=lambda: self.imageHandler.changeScale(.1),
                  bg='#8fc9e8').grid(row=row, column=1, sticky="nsew")
        row += 1

        tk.Button(self.left_frame, text="⟲",
                  command=lambda: self.imageHandler.rotateImage(90),
                  bg='#8fc9e8').grid(row=row, column=0, sticky="nsew")
        tk.Button(self.left_frame, text="⟳",
                  command=lambda: self.imageHandler.rotateImage(-90),
                  bg='#8fc9e8').grid(row=row, column=1, sticky="nsew")
        row += 1

        # ── Arrow pad ────────────────────────────────────────────────────────
        arrow_frame = tk.Frame(self.left_frame, bg='#FFFDEE')
        for i in range(3):
            arrow_frame.grid_rowconfigure(i, weight=1)
            arrow_frame.grid_columnconfigure(i, weight=1)
        arrow_frame.grid(row=row, column=0, columnspan=2, sticky="nsew")
        row += 1

        self.slider = tk.Scale(arrow_frame, from_=1, to=10,
                               orient=tk.HORIZONTAL, bg='#8fc9e8')
        self.slider.grid(row=3, column=0, columnspan=3, sticky="nsew")
        tk.Label(arrow_frame, text="Pixel move distance",
                 bg='#8fc9e8').grid(row=4, column=0, columnspan=3, sticky="nsew")

        tk.Button(arrow_frame, text="↑",
                  command=lambda: self.imageHandler.changeOffset(0, -self.slider.get()),
                  bg='#8fc9e8').grid(row=0, column=1, sticky="nsew")
        tk.Button(arrow_frame, text="←",
                  command=lambda: self.imageHandler.changeOffset(-self.slider.get(), 0),
                  bg='#8fc9e8').grid(row=1, column=0, sticky="nsew")
        tk.Button(arrow_frame, text="Reset",
                  command=lambda: self.imageHandler.resetImage(
                      self.imageHandler.imageSelected),
                  bg='#8fc9e8').grid(row=1, column=1, sticky="nsew")
        tk.Button(arrow_frame, text="→",
                  command=lambda: self.imageHandler.changeOffset(self.slider.get(), 0),
                  bg='#8fc9e8').grid(row=1, column=2, sticky="nsew")
        tk.Button(arrow_frame, text="↓",
                  command=lambda: self.imageHandler.changeOffset(0, self.slider.get()),
                  bg='#8fc9e8').grid(row=2, column=1, sticky="nsew")

        # ── Export ───────────────────────────────────────────────────────────
        tk.Button(self.left_frame, text="Export to data/",
                  command=self.imageHandler.exportImages,
                  bg='#e88fc9').grid(row=row, column=0, columnspan=2, sticky="nsew")
        row += 1

        # ── Preview canvas ───────────────────────────────────────────────────
        self.right_frame = tk.Frame(self.root, bg='#8fc9e8')
        self.right_frame.grid(row=0, column=1)

        self.canvas = Canvas(self.right_frame,
                             width=self.outerFrameX,
                             height=self.outerFrameY,
                             bg='#8fc9e8')
        self.canvas.pack()
        self.canvas.bind('<KeyPress>',   self.key_press)
        self.canvas.bind('<KeyRelease>', self.key_release)
        self.canvas.bind('<Button-1>',   self.canvas_click)
        self.canvas.focus_set()

    # ── Event handlers ───────────────────────────────────────────────────────
    def on_selection_change(self, event):
        sel = event.widget.curselection()
        if sel:
            self.imageHandler.imageSelected = sel[0]
            self.imageHandler.canvasImage(self.imageHandler.imageSelected)

    def canvas_click(self, event):
        self.canvas.focus_set()

    def run(self):
        self.root.mainloop()

    def key_press(self, event):
        if   event.keysym == 'Up':     self.up_pressed    = True
        elif event.keysym == 'Down':   self.down_pressed  = True
        elif event.keysym == 'Left':   self.left_pressed  = True
        elif event.keysym == 'Right':  self.right_pressed = True
        elif event.keysym == 'plus':   self.plus_pressed  = True
        elif event.keysym == 'minus':  self.minus_pressed = True
        elif event.keysym == 'Delete': self.delete_pressed = True
        elif event.keysym == 'period': self.dot_pressed   = True
        elif event.keysym == 'comma':  self.comma_pressed = True

    def key_release(self, event):
        if   event.keysym == 'Up':     self.up_pressed    = False
        elif event.keysym == 'Down':   self.down_pressed  = False
        elif event.keysym == 'Left':   self.left_pressed  = False
        elif event.keysym == 'Right':  self.right_pressed = False
        elif event.keysym == 'plus':   self.plus_pressed  = False
        elif event.keysym == 'minus':  self.minus_pressed = False
        elif event.keysym == 'Delete': self.delete_pressed = False
        elif event.keysym == 'period': self.dot_pressed   = False
        elif event.keysym == 'comma':  self.comma_pressed = False

    def update(self):
        v = self.slider.get()
        if self.up_pressed:    self.imageHandler.changeOffset(0,  -v)
        if self.down_pressed:  self.imageHandler.changeOffset(0,   v)
        if self.left_pressed:  self.imageHandler.changeOffset(-v,  0)
        if self.right_pressed: self.imageHandler.changeOffset( v,  0)
        if self.plus_pressed:  self.imageHandler.changeScale(.1)
        if self.minus_pressed: self.imageHandler.changeScale(-.1)
        if self.delete_pressed: self.imageHandler.deleteImage()
        if self.dot_pressed:   self.imageHandler.rotateImage(-90)
        if self.comma_pressed: self.imageHandler.rotateImage(90)
        self.root.after(100, self.update)


if __name__ == "__main__":
    app = ImageApp()
    app.run()
