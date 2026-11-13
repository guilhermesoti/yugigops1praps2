"""The Duelists tab's Bust section: the duelist's large character, the one
the campaign's dialogue and a tower's stories stand on the box
(notes/more-duelists.md, "bust"; bust.py).

Down the left, its expressions in the order the game numbers them (the
default first); in the middle, the screen at the console's 320 x 240 with
the chosen expression, a talking frame or the blink standing where the game
stands it; on the right, its pictures, and for a disc duelist the option of
making it the campaign's character too ("replace_bust"), with what that
changes said beside it.
"""
from __future__ import annotations

import tkinter as tk
from pathlib import Path
from tkinter import messagebox, ttk

from . import bust as bustm, file_dialogs, pngio, roster, tower
from .gamedata import DUELIST_NAMES
from .widgets import FormDialog, px, ui_font

STAGE = "#1b2130"


class BustView:
    def __init__(self, tab, parent):
        self.tab = tab
        self.frame = ttk.Frame(parent, padding=(0, 6, 0, 0))
        self.shown = None
        self.which = tk.StringVar(value="image")
        self.side = tk.StringVar(value="left")
        self.replace = tk.BooleanVar(value=False)
        left = ttk.Frame(self.frame)
        left.pack(side="left", fill="y")
        self.list = ttk.Treeview(left, columns=("n", "frames"), show="tree headings", height=8, selectmode="browse")
        self.list.heading("#0", text="Expression")
        self.list.heading("n", text="#")
        self.list.heading("frames", text="Frames")
        self.list.column("#0", width=px(left, 84), stretch=True)
        self.list.column("n", width=px(left, 24), stretch=False, anchor="e")
        self.list.column("frames", width=px(left, 72), stretch=False)
        self.list.bind("<<TreeviewSelect>>", lambda e: self.draw())
        self.list.bind("<Double-1>", lambda e: self.rename())
        rows = ttk.Frame(left)
        rows.pack(side="bottom", fill="x", pady=(4, 0))
        self.list.pack(fill="y", expand=True)
        self.add_button = ttk.Button(rows, text="Add...", width=8, command=self.add)
        self.add_button.grid(row=0, column=0, sticky="we")
        self.rename_button = ttk.Button(rows, text="Rename...", width=9, command=self.rename)
        self.rename_button.grid(row=0, column=1, sticky="we", padx=(2, 0))
        self.remove_button = ttk.Button(rows, text="Remove", width=8, command=self.remove)
        self.remove_button.grid(row=1, column=0, sticky="we", pady=(2, 0))
        self.default_button = ttk.Button(rows, text="Default", width=9, command=self.make_default)
        self.default_button.grid(row=1, column=1, sticky="we", padx=(2, 0), pady=(2, 0))
        self.up_button = ttk.Button(rows, text="▲", width=2, command=lambda: self.move(-1))
        self.up_button.grid(row=0, column=2, sticky="w", padx=(2, 0))
        self.down_button = ttk.Button(rows, text="▼", width=2, command=lambda: self.move(1))
        self.down_button.grid(row=1, column=2, sticky="w", padx=(2, 0), pady=(2, 0))
        middle = ttk.Frame(self.frame)
        middle.pack(side="left", anchor="n", padx=(6, 6))
        self.canvas = tk.Canvas(middle, width=bustm.SCREEN[0], height=bustm.SCREEN[1], background=STAGE,
                                highlightthickness=0, borderwidth=0)
        self.canvas.pack(anchor="w")
        pick = ttk.Frame(middle)
        pick.pack(anchor="w", pady=(2, 0))
        self.frame_buttons = []
        for value, text in (("image", "Picture"), ("talk0", "Talk 1"), ("talk1", "Talk 2"), ("blink", "Blink")):
            button = ttk.Radiobutton(pick, text=text, value=value, variable=self.which, command=self.draw)
            button.pack(side="left", padx=(0, 4))
            self.frame_buttons.append((value, button))
        ttk.Radiobutton(pick, text="left", value="left", variable=self.side, command=self.draw).pack(
            side="left", padx=(8, 0))
        ttk.Radiobutton(pick, text="right", value="right", variable=self.side, command=self.draw).pack(side="left")
        self.caption = ttk.Label(middle, style="Hint.TLabel", justify="left", wraplength=px(middle, 320))
        self.caption.pack(anchor="w", pady=(2, 0))
        right = ttk.Frame(self.frame)
        right.pack(side="left", anchor="n", fill="x")
        width = px(right, 170)
        self.title = ttk.Label(right, font=ui_font(10), justify="left", wraplength=width)
        self.title.pack(anchor="w")
        buttons = ttk.Frame(right)
        buttons.pack(anchor="w", pady=(4, 0))
        self.picture_button = ttk.Button(buttons, text="Picture...", command=self.choose_picture)
        self.picture_button.grid(row=0, column=0, sticky="we")
        self.talk_button = ttk.Button(buttons, text="Talking...", command=self.add_talk)
        self.talk_button.grid(row=0, column=1, sticky="we", padx=(2, 0))
        self.untalk_button = ttk.Button(buttons, text="No talk", command=self.drop_talk)
        self.untalk_button.grid(row=1, column=1, sticky="we", padx=(2, 0), pady=(2, 0))
        self.blink_button = ttk.Button(buttons, text="Blink...", command=self.choose_blink)
        self.blink_button.grid(row=1, column=0, sticky="we", pady=(2, 0))
        self.unblink_button = ttk.Button(buttons, text="No blink", command=self.drop_blink)
        self.unblink_button.grid(row=2, column=0, sticky="we", pady=(2, 0))
        self.drop_button = ttk.Button(buttons, text="No bust", command=self.drop)
        self.drop_button.grid(row=2, column=1, sticky="we", padx=(2, 0), pady=(2, 0))
        self.replace_box = ttk.Checkbutton(right, text="The campaign's character too", variable=self.replace,
                                           command=self.toggle_replace)
        self.replace_box.pack(anchor="w", pady=(8, 0))
        self.warning = ttk.Label(right, style="Hint.TLabel", justify="left", wraplength=width)
        self.warning.pack(anchor="w", fill="x")
        self.hint = ttk.Label(right, style="Hint.TLabel", justify="left", wraplength=width, text=(
            "Talking and blink frames are the picture with the mouth open or the eyes shut: the game draws "
            "them over it where they differ, in 8 x 8 squares kept in what the picture leaves of its 256 x 256 "
            "page. A 256 x 256 picture leaves none, so it never talks or blinks; 192 x 256 leaves 256 squares, "
            "128 x 192 leaves 640. The game numbers the expressions as listed: 0 the default."))
        self.hint.pack(anchor="w", fill="x", pady=(6, 0))
        # What an edit did to the tower's stories that name the bust.
        self.story_note = ttk.Label(right, style="Hint.TLabel", justify="left", wraplength=width)
        self.story_note.pack(anchor="w", fill="x", pady=(6, 0))

    # --- what is shown ------------------------------------------------------------------

    @property
    def project(self):
        return self.tab.project

    def subject(self):
        return self.tab.subject()

    def bust(self):
        return roster.bust_of(self.project, self.subject()) if self.project is not None else None

    def chosen(self):
        b = self.bust()
        selection = self.list.selection()
        if b is None or not selection:
            return None
        return next((e for e in b.expressions if str(id(e)) == selection[0]), None)

    def fill(self):
        if self.project is None:
            return
        s, b = self.subject(), self.bust()
        keep = self.chosen()
        self.list.delete(*self.list.get_children())
        if b is not None:
            for n, e in enumerate(bustm.order(b)):
                frames = []
                if e.talk:
                    frames.append(f"talk {len(e.talk)}")
                if e.blink is not None:
                    frames.append("blink")
                frames = [", ".join(frames)] if frames else []
                self.list.insert("", "end", iid=str(id(e)), text=e.name, values=(n, ", ".join(frames) or "-"))
            pick = keep if keep in b.expressions else (bustm.order(b)[0] if b.expressions else None)
            if pick is not None:
                self.list.selection_set(str(id(pick)))
        disc = isinstance(s, int)
        e = roster.replacement(self.project, s) if disc else s
        name = roster.shown_name(self.project, s)
        if b is None:
            self.title.configure(text=f"{name} has no bust of its own")
        else:
            first = bustm.order(b)[0].name if b.expressions else ""
            self.title.configure(text=f"{name}'s bust: {len(b.expressions)} expression"
                                      f"{'s' if len(b.expressions) != 1 else ''}, the default {first}")
        self.replace.set(bool(e is not None and e.replace_bust))
        self.replace_box.state(["!disabled"] if disc and b is not None and s != 0 else ["disabled"])
        self.warning.configure(text=self.replace_text(s, b, e))
        self.draw()

    def replace_text(self, s, b, e) -> str:
        """What "replace_bust" does, said beside the option."""
        if not isinstance(s, int):
            return "An added duelist's bust is its own wherever it is shown; the campaign does not show it."
        character = bustm.campaign_character(s)
        if s == 0 or not character:
            return f"The campaign shows {DUELIST_NAMES[s]} as no character: there is nothing to replace."
        others = [DUELIST_NAMES[d] for d in bustm.sharing(s) if d != s]
        text = (f"Off, the bust only adds expressions a tower's stories may name; the campaign keeps its own "
                f"character {character:#04x}.")
        if b is not None and e is not None and e.replace_bust:
            text = (f"On: CHANGES THE CAMPAIGN. Every scene that shows character {character:#04x} draws this bust "
                    "instead, its expressions 0-3 the bust's first four (the default, then as listed).")
            if others:
                text += " It is " + ", ".join(others) + " too."
        return text

    def draw(self):
        self.canvas.delete("all")
        b, e = self.bust(), self.chosen()
        which = self.which.get()
        for value, button in self.frame_buttons:
            ok = e is not None and (value == "image" or (value == "blink" and e.blink is not None) or
                                    (value.startswith("talk") and int(value[4:]) < len(e.talk)))
            button.state(["!disabled"] if ok else ["disabled"])
            if not ok and which == value:
                which = "image"
                self.which.set("image")
        image = None
        if e is not None:
            image = bustm.frame(e, "image" if which == "image" else "blink" if which == "blink" else
                                ("talk", int(which[4:])))
        screen = bustm.preview(image, self.side.get())
        self.shown = tk.PhotoImage(master=self.canvas, data=pngio.ppm(screen), format="PPM")
        self.canvas.create_image(0, 0, image=self.shown, anchor="nw")
        if image is None:
            self.canvas.create_text(bustm.SCREEN[0] // 2, 80, fill="#c4c8cd", justify="center", width=280,
                                    text="No bust: the campaign's character\n(Add... gives it one)" if b is None
                                    else "")
            self.caption.configure(text="")
        else:
            fit = bustm.fitted(image)
            first = bustm.order(b)[0] if b is not None and b.expressions else e
            size = bustm.size_of(first.image) or image.size
            free = bustm.room(size)[2]
            self.caption.configure(text=f"{image.width}×{image.height}"
                                        + ("" if fit.size == image.size else f", shrunk to {fit.width}×{fit.height}")
                                        + ", at the console's 320×240; room for its frames' changes: "
                                        + f"{free} of the page's {bustm.PAGE_CELLS} squares")
        # A bust a picture of which could not be read is written back as it
        # was, pictures and all: an edit would not be saved, so none is made.
        kept = b is not None and b.unread
        if kept:
            self.caption.configure(text="A picture of this bust could not be read: it is kept as written, its "
                                        "files as they are. Mend them and open the mod again to edit it.")
        has = e is not None and not kept
        for button in (self.rename_button, self.remove_button, self.default_button, self.up_button,
                       self.down_button, self.picture_button, self.talk_button, self.blink_button):
            button.state(["!disabled"] if has else ["disabled"])
        self.untalk_button.state(["!disabled"] if has and e.talk else ["disabled"])
        self.unblink_button.state(["!disabled"] if has and e.blink is not None else ["disabled"])
        self.drop_button.state(["!disabled"] if b is not None else ["disabled"])
        self.add_button.state(["!disabled"] if self.subject() != 0 and not kept else ["disabled"])

    # --- edits ----------------------------------------------------------------------------

    def _is_subject(self, who) -> bool:
        s = roster.named(self.project, who)
        return s is not None and (s == self.subject() if isinstance(s, int) else s is self.subject())

    def _stories(self):
        return self.project.other.get("tower") if self.project is not None else None

    def _stories_changed(self, count, text):
        """The tower's stories edited to follow the bust: the Tower tab shows
        them again, and the page says so."""
        self.story_note.configure(text=text if count else "")
        if count:
            towers = getattr(self.tab.app, "tower", None)
            if towers is not None:
                towers.refresh()
        return count

    def _numbers_note(self, what):
        """A tower's stories asking for the bust by number: after the order
        changed, they show other expressions."""
        count = tower.numbered_expressions(self._stories(), self._is_subject,
                                           roster.shows_bust(self.project, self.subject()))
        self.story_note.configure(text=f"{what}: {count} story side{'s' if count != 1 else ''} of the Tower tab "
                                       f"ask{'' if count != 1 else 's'} for this bust by number, which follows the "
                                       "order: name the expression instead (expression angry)." if count else "")
        return count

    def changed(self, chosen=None):
        self.tab.changed(self.subject())
        b = self.bust()
        if chosen is not None and b is not None and chosen in b.expressions and self.list.exists(str(id(chosen))):
            self.list.selection_set(str(id(chosen)))
            self.draw()

    def ask_png(self, title):
        path = file_dialogs.askopenfilename(parent=self.frame, title=title,
                                            filetypes=[("PNG pictures", "*.png"), ("All files", "*")])
        return self.read_png(path) if path else None

    def read_png(self, path):
        try:
            data = Path(path).read_bytes()
            pngio.decode(data)
            return data
        except (OSError, pngio.PngError) as problem:
            messagebox.showerror("Bust", f"{path} cannot be used: {problem}", parent=self.frame)
            return None

    def name_dialog(self, title, value, on_name):
        fields = {}

        def build(dialog, body):
            ttk.Label(body, text="Name").grid(row=0, column=0, sticky="w")
            fields["name"] = tk.StringVar(value=value)
            entry = ttk.Entry(body, textvariable=fields["name"], width=24)
            entry.grid(row=0, column=1, sticky="we", padx=(4, 0))
            entry.focus_set()
            ttk.Label(body, style="Hint.TLabel", text="A tower's story names it: {\"duelist\": ..., "
                      "\"expression\": \"<name>\"}").grid(row=1, column=0, columnspan=2, sticky="w", pady=(4, 0))

        def ok(dialog):
            return on_name(fields["name"].get().strip())
        return FormDialog(self.frame, title, build, ok)

    def add(self, path=None, name=None):
        """A new expression from a PNG (asked for when `path` is None)."""
        s = self.subject()
        if self.project is None or s == 0:
            return None
        data = self.read_png(path) if path else self.ask_png("A picture for a new expression (a PNG with clear parts)")
        if data is None:
            return None
        b = self.bust()

        def named(text):
            if b is None:
                problem = bustm.check_name(bustm.Bust(), text)
                if problem:
                    return problem
                made = roster.give_bust(self.project, s, data, text)
                self.changed(made.expressions[0])
                return None
            problem = bustm.check_name(b, text)
            if problem:
                return problem
            e = bustm.add(b, text, data)
            self.changed(e)
            return None
        if name is not None:
            return named(name)
        return self.name_dialog("A new expression", "calm" if b is None else "", named)

    def rename(self, name=None):
        b, e = self.bust(), self.chosen()
        if e is None:
            return None

        def named(text):
            problem = bustm.check_name(b, text, but=e)
            if problem:
                return problem
            old = e.name
            bustm.rename(b, e, text)
            count = tower.rename_expression(self._stories(), self._is_subject, old, e.name)
            self.changed(e)
            self._stories_changed(count, f"{count} story side{'s' if count != 1 else ''} of the Tower tab naming "
                                         f"\"{old}\" now name{'' if count != 1 else 's'} \"{e.name}\".")
            return None
        if name is not None:
            return named(name)
        return self.name_dialog(f"Rename \"{e.name}\"", e.name, named)

    def remove(self):
        b, e = self.bust(), self.chosen()
        if e is None:
            return
        if len(b.expressions) == 1:
            self.drop()
            return
        bustm.remove(b, e)
        default = bustm.order(b)[0].name
        count = tower.rename_expression(self._stories(), self._is_subject, e.name, default)
        self.changed()
        self._stories_changed(count, f"{count} story side{'s' if count != 1 else ''} of the Tower tab named "
                                     f"\"{e.name}\", which is gone: {'they' if count != 1 else 'it'} now name the "
                                     f"default, \"{default}\" -- check them.")

    def make_default(self):
        b, e = self.bust(), self.chosen()
        if e is not None and b.default != e.name:
            b.default = e.name
            self.changed(e)
            self._numbers_note(f"\"{e.name}\" is the default, 0")

    def move(self, by):
        b, e = self.bust(), self.chosen()
        if e is None:
            return
        # The list shows the game's order: the default stays first.
        rest = [x for x in bustm.order(b)[1:]]
        if e not in rest:
            return
        at = rest.index(e)
        to = max(0, min(len(rest) - 1, at + by))
        rest.insert(to, rest.pop(at))
        first = bustm.order(b)[0]
        b.expressions = [first] + rest
        self.changed(e)
        self._numbers_note(f"\"{e.name}\" is {bustm.number(b, e)} now")

    def choose_picture(self, path=None):
        e = self.chosen()
        if e is None:
            return
        data = self.read_png(path) if path else self.ask_png(f"The picture of \"{e.name}\"")
        if data is not None:
            if roster.picture_shared(self.project, e.image_path):
                e.image_path = None     # a file of its own: the others keep theirs
            e.image = data
            self.changed(e)

    def add_talk(self, path=None):
        e = self.chosen()
        if e is None:
            return
        if len(e.talk) >= bustm.TALK_FRAMES:
            messagebox.showinfo("Bust", f"An expression has at most {bustm.TALK_FRAMES} talking frames.",
                                parent=self.frame)
            return
        data = self.read_png(path) if path else self.ask_png(f"\"{e.name}\" talking: the picture with the mouth open")
        if data is not None:
            e.talk.append(data)
            e.talk_paths.append(None)
            self.changed(e)

    def drop_talk(self):
        e = self.chosen()
        if e is not None and e.talk:
            e.talk.pop()
            e.talk_paths = e.talk_paths[:len(e.talk)]
            self.changed(e)

    def choose_blink(self, path=None):
        e = self.chosen()
        if e is None:
            return
        data = self.read_png(path) if path else self.ask_png(f"\"{e.name}\" blinking: the picture with the eyes shut")
        if data is not None:
            if roster.picture_shared(self.project, e.blink_path):
                e.blink_path = None
            e.blink = data
            self.changed(e)

    def drop_blink(self):
        e = self.chosen()
        if e is not None and e.blink is not None:
            e.blink = None
            e.blink_path = None
            self.changed(e)

    def drop(self, ask=True):
        s = self.subject()
        if self.bust() is None:
            return
        if ask and not messagebox.askyesno("Bust", f"Take {roster.shown_name(self.project, s)}'s bust out of the "
                                           "mod? Its pictures go with it.", parent=self.frame):
            return
        roster.drop_bust(self.project, s)
        self.changed()

    def toggle_replace(self):
        s = self.subject()
        if not isinstance(s, int) or self.bust() is None:
            self.replace.set(False)
            return
        on = self.replace.get()
        if on and not messagebox.askyesno(
                "The campaign's character too",
                f"This changes the campaign: every scene that shows {DUELIST_NAMES[s]}'s character draws this bust "
                "instead" + (", for " + ", ".join(DUELIST_NAMES[d] for d in bustm.sharing(s) if d != s) + " too"
                             if len(bustm.sharing(s)) > 1 else "") + ". Turn it on?", parent=self.frame):
            self.replace.set(False)
            return
        roster.set_replace_bust(self.project, s, on)
        self.changed()
