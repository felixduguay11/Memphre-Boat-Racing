"""
Générateur de graphiques à partir d'un fichier JSON.

Dépendances :  pip install pandas matplotlib
Lancement :    python json_graph_ui.py  [fichier.json | fichier.jsonl]

Formats JSON acceptés :
  - une liste d'objets :            [{"date": "...", "ventes": 12}, ...]
  - un objet de listes :            {"date": [...], "ventes": [...]}
  - un objet contenant une liste :  {"data": [{...}, {...}]}
  - du JSONL (un objet par ligne), ex. logs de télémétrie
Les objets imbriqués sont aplatis (ex. "stats.prix") et les listes d'objets
sont éclatées par id (ex. "esc": [{"id": 10, "erpm": ...}] -> "esc10.erpm").
"""

import json
import signal
import sys
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

import pandas as pd
import matplotlib
matplotlib.use("TkAgg")
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk

INDEX_LABEL = "(index / numéro de ligne)"
CHART_TYPES = ["Ligne", "Barres", "Nuage de points", "Aire", "Histogramme"]

# --------------------------------------------------------------------------- #
#  Grandeurs et unités, selon le nom du champ (sans le préfixe "esc10.")
#  Ajoutez ici vos propres champs : "nom": ("Libellé", "Grandeur", "unité")
#  La "Grandeur" sert à regrouper l'axe Y (ex. i_in et i_mot = Courant).
# --------------------------------------------------------------------------- #
UNITS = {
    "erpm":   ("Vitesse électrique", "Vitesse",     "ERPM"),
    "target": ("Consigne",           "Vitesse",     "ERPM"),
    "ramped": ("Consigne rampée",    "Vitesse",     "ERPM"),
    "i_in":   ("Courant d'entrée",   "Courant",     "A"),
    "i_mot":  ("Courant moteur",     "Courant",     "A"),
    "v_in":   ("Tension d'entrée",   "Tension",     "V"),
    "duty":   ("Rapport cyclique",   "Rapport cyclique", "0–1"),
    "t_fet":  ("Température FET",    "Température", "°C"),
    "t_mot":  ("Température moteur", "Température", "°C"),
    "t":      ("Temps",              "Temps",       "s"),
    "t_pi":   ("Temps Pi",           "Temps",       "s"),
    "ok":     ("Statut ESC",         "Statut",      ""),
}


# Conversions appliquées au chargement : "champ": facteur multiplicatif
# (t est enregistré en ms dans le log -> converti en secondes)
CONVERSIONS = {
    "t": 0.001,
}


def describe_column(col: str):
    """'esc10.i_mot' -> ('Courant moteur (ESC 10)', 'Courant', 'A')"""
    prefix, _, base = col.rpartition(".")
    label, quantity, unit = UNITS.get(base, (base, base, ""))
    if prefix:
        source = prefix.upper().replace("ESC", "ESC ") if prefix.startswith("esc") else prefix
        label = f"{label} ({source})"
    return label, quantity, unit


def axis_label(cols):
    """Libellé d'axe : 'Courant (A)' si tout est de même grandeur."""
    infos = [describe_column(c) for c in cols]
    groups = []
    for _, quantity, unit in infos:
        text = f"{quantity} ({unit})" if unit else quantity
        if text not in groups:
            groups.append(text)
    return " / ".join(groups)


# --------------------------------------------------------------------------- #
#  Chargement du JSON
# --------------------------------------------------------------------------- #
def flatten_record(obj, prefix=""):
    """Aplatit un objet : dicts imbriqués -> 'a.b', listes d'objets -> 'esc10.erpm'
    (utilise le champ 'id' s'il existe, sinon la position dans la liste)."""
    out = {}
    for key, val in obj.items():
        name = f"{prefix}{key}"
        if isinstance(val, dict):
            out.update(flatten_record(val, name + "."))
        elif isinstance(val, list) and val and all(isinstance(v, dict) for v in val):
            for i, item in enumerate(val):
                tag = item.get("id", i)
                sub = {k: v for k, v in item.items() if k != "id"}
                out.update(flatten_record(sub, f"{name}{tag}."))
        elif isinstance(val, list):
            out[name] = str(val)
        else:
            out[name] = val
    return out


def read_json_or_jsonl(path: str):
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        # JSONL : un objet JSON par ligne (les lignes corrompues sont ignorées)
        rows = []
        for line in text.splitlines():
            line = line.strip()
            if line:
                try:
                    rows.append(json.loads(line))
                except json.JSONDecodeError:
                    pass
        if not rows:
            raise ValueError("Ni JSON ni JSONL valide.")
        return rows


def load_json_to_dataframe(path: str) -> pd.DataFrame:
    raw = read_json_or_jsonl(path)

    if isinstance(raw, list) and raw and all(isinstance(r, dict) for r in raw):
        df = pd.DataFrame([flatten_record(r) for r in raw])
    elif isinstance(raw, list):
        df = pd.json_normalize(raw)
    elif isinstance(raw, dict):
        # Cherche une clé qui contient une liste d'objets
        list_keys = [k for k, v in raw.items()
                     if isinstance(v, list) and v and isinstance(v[0], dict)]
        if list_keys:
            df = pd.DataFrame([flatten_record(r) for r in raw[list_keys[0]]])
        elif all(isinstance(v, list) for v in raw.values()):
            df = pd.DataFrame(raw)          # objet de listes (colonnes)
        else:
            df = pd.json_normalize(raw)     # objet simple -> une ligne
    else:
        raise ValueError("Le JSON doit contenir une liste ou un objet.")

    # Convertit ce qui ressemble à des nombres ou à des dates
    for col in df.columns:
        if (pd.api.types.is_object_dtype(df[col])
                or pd.api.types.is_string_dtype(df[col])):
            num = pd.to_numeric(df[col], errors="coerce")
            if num.notna().mean() > 0.9:
                df[col] = num
                continue
            try:
                dates = pd.to_datetime(df[col], errors="coerce")
                if dates.notna().mean() > 0.9:
                    df[col] = dates
            except (TypeError, ValueError):
                pass
    # Conversions d'unités (ex. ms -> s)
    for col in df.columns:
        factor = CONVERSIONS.get(col.rpartition(".")[2])
        if factor is not None and pd.api.types.is_numeric_dtype(df[col]):
            df[col] = df[col] * factor
    return df


# --------------------------------------------------------------------------- #
#  Interface
# --------------------------------------------------------------------------- #
class JsonGraphApp(tk.Tk):
    def __init__(self, initial_file: str | None = None):
        super().__init__()
        self.title("Graphiques JSON")
        self.geometry("1150x700")
        self.minsize(1480, 920)

        self.df: pd.DataFrame | None = None
        self._build_ui()

        # Fermeture propre : bouton X, Ctrl+Q, Échap et Ctrl+C dans le terminal
        self.protocol("WM_DELETE_WINDOW", self.on_close)
        self.bind("<Control-q>", lambda e: self.on_close())
        self.bind("<Escape>", lambda e: self.on_close())
        signal.signal(signal.SIGINT, lambda *a: self.on_close())
        self._keep_python_awake()

        if initial_file:
            self.open_file(initial_file)

    # ---------- construction ----------
    def _build_ui(self):
        panel = ttk.Frame(self, padding=10)
        panel.pack(side=tk.LEFT, fill=tk.Y)

        ttk.Button(panel, text="Ouvrir un fichier JSON…",
                   command=self.open_file).pack(fill=tk.X)
        self.file_label = ttk.Label(panel, text="Aucun fichier chargé",
                                    foreground="gray", wraplength=240)
        self.file_label.pack(fill=tk.X, pady=(4, 12))

        # Type de graphique
        ttk.Label(panel, text="Type de graphique").pack(anchor=tk.W)
        self.chart_type = tk.StringVar(value=CHART_TYPES[0])
        ttk.Combobox(panel, textvariable=self.chart_type, values=CHART_TYPES,
                     state="readonly").pack(fill=tk.X, pady=(0, 10))

        # Axe X
        ttk.Label(panel, text="Axe X").pack(anchor=tk.W)
        self.x_var = tk.StringVar()
        self.x_combo = ttk.Combobox(panel, textvariable=self.x_var, state="readonly")
        self.x_combo.pack(fill=tk.X, pady=(0, 10))

        # Axe(s) Y
        ttk.Label(panel, text="Axe Y (Ctrl/Maj pour plusieurs)").pack(anchor=tk.W)
        y_frame = ttk.Frame(panel)
        y_frame.pack(fill=tk.BOTH, expand=True, pady=(0, 10))
        self.y_list = tk.Listbox(y_frame, selectmode=tk.EXTENDED,
                                 exportselection=False, height=8)
        sb = ttk.Scrollbar(y_frame, orient=tk.VERTICAL, command=self.y_list.yview)
        self.y_list.configure(yscrollcommand=sb.set)
        self.y_list.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        sb.pack(side=tk.RIGHT, fill=tk.Y)

        # Quantité de données
        data_box = ttk.LabelFrame(panel, text="Quantité de données", padding=8)
        data_box.pack(fill=tk.X, pady=(0, 10))

        self.all_rows = tk.BooleanVar(value=True)
        ttk.Checkbutton(data_box, text="Tout prendre", variable=self.all_rows,
                        command=self._toggle_rows).pack(anchor=tk.W)

        row = ttk.Frame(data_box)
        row.pack(fill=tk.X, pady=4)
        ttk.Label(row, text="Nombre :").pack(side=tk.LEFT)
        self.n_rows = tk.IntVar(value=100)
        self.n_spin = ttk.Spinbox(row, from_=1, to=1_000_000, width=10,
                                  textvariable=self.n_rows)
        self.n_spin.pack(side=tk.LEFT, padx=5)

        self.n_scale = ttk.Scale(data_box, from_=1, to=100, orient=tk.HORIZONTAL,
                                 command=lambda v: self.n_rows.set(int(float(v))))
        self.n_scale.pack(fill=tk.X)

        ttk.Label(data_box, text="Prendre :").pack(anchor=tk.W, pady=(6, 0))
        self.take_from = tk.StringVar(value="début")
        for label in ("début", "fin", "aléatoire"):
            ttk.Radiobutton(data_box, text=f"Au {label}" if label != "aléatoire"
                            else "Échantillon aléatoire",
                            value=label, variable=self.take_from).pack(anchor=tk.W)

        # Options
        self.sort_x = tk.BooleanVar(value=True)
        ttk.Checkbutton(panel, text="Trier selon l'axe X",
                        variable=self.sort_x).pack(anchor=tk.W)
        self.grid_on = tk.BooleanVar(value=True)
        ttk.Checkbutton(panel, text="Afficher la grille",
                        variable=self.grid_on).pack(anchor=tk.W, pady=(0, 10))

        ttk.Button(panel, text="Générer le graphique",
                   command=self.plot).pack(fill=tk.X, pady=(0, 4))
        ttk.Button(panel, text="Enregistrer en PNG…",
                   command=self.save_png).pack(fill=tk.X)

        # Zone graphique
        right = ttk.Frame(self, padding=(0, 10, 10, 10))
        right.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True)
        self.fig = Figure(figsize=(7, 5), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self.canvas = FigureCanvasTkAgg(self.fig, master=right)
        NavigationToolbar2Tk(self.canvas, right).update()
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        self._placeholder("Ouvrez un fichier JSON pour commencer")

        self._toggle_rows()

    # ---------- fermeture ----------
    def _keep_python_awake(self):
        # Tk bloque les signaux ; ce petit réveil régulier laisse passer Ctrl+C
        self._awake_id = self.after(200, self._keep_python_awake)

    def on_close(self):
        try:
            self.after_cancel(self._awake_id)
        except Exception:
            pass
        import matplotlib.pyplot as plt
        plt.close("all")
        self.quit()      # arrête mainloop
        self.destroy()   # détruit la fenêtre

    # ---------- helpers ----------
    def _placeholder(self, text):
        self.ax.clear()
        self.ax.text(0.5, 0.5, text, ha="center", va="center",
                     color="gray", transform=self.ax.transAxes)
        self.ax.set_axis_off()
        self.canvas.draw()

    def _toggle_rows(self):
        state = "disabled" if self.all_rows.get() else "normal"
        self.n_spin.configure(state=state)
        self.n_scale.configure(state=state)

    def _numeric_columns(self):
        return [c for c in self.df.columns
                if pd.api.types.is_numeric_dtype(self.df[c])
                and not pd.api.types.is_bool_dtype(self.df[c])]

    # ---------- actions ----------
    def open_file(self, path: str | None = None):
        if not path:
            path = filedialog.askopenfilename(
                filetypes=[("JSON / JSONL", "*.json *.jsonl"), ("Tous les fichiers", "*.*")])
            if not path:
                return
        try:
            self.df = load_json_to_dataframe(path)
        except Exception as e:
            messagebox.showerror("Lecture impossible", f"{e}")
            return

        if self.df.empty:
            messagebox.showwarning("Fichier vide", "Aucune donnée trouvée dans ce JSON.")
            return

        n = len(self.df)
        self.file_label.configure(text=f"{path.split('/')[-1]}  —  {n} lignes, "
                                       f"{len(self.df.columns)} colonnes",
                                  foreground="black")

        cols = list(self.df.columns)
        self.x_combo.configure(values=[INDEX_LABEL] + cols)
        time_names = ["t", "time", "temps", "timestamp", "date", "t_pi"]
        default_x = next((c for n in time_names for c in cols if c.lower() == n),
                         cols[0] if cols else INDEX_LABEL)
        self.x_var.set(default_x)

        numeric = self._numeric_columns()
        self.y_list.delete(0, tk.END)
        for c in numeric:
            self.y_list.insert(tk.END, c)
        # Présélectionne la première colonne numérique différente de X
        for i, c in enumerate(numeric):
            if c != self.x_var.get():
                self.y_list.selection_set(i)
                break

        self.n_spin.configure(to=n)
        self.n_scale.configure(to=n)
        self.n_rows.set(min(100, n))
        self.n_scale.set(min(100, n))

        self._placeholder("Choisissez les axes puis « Générer le graphique »")

    def _subset(self) -> pd.DataFrame:
        df = self.df
        if self.all_rows.get():
            return df
        try:
            n = max(1, min(int(self.n_rows.get()), len(df)))
        except (tk.TclError, ValueError):
            n = len(df)
        mode = self.take_from.get()
        if mode == "fin":
            return df.tail(n)
        if mode == "aléatoire":
            return df.sample(n, random_state=None)
        return df.head(n)

    def plot(self):
        if self.df is None:
            messagebox.showinfo("Aucune donnée", "Ouvrez d'abord un fichier JSON.")
            return

        y_cols = [self.y_list.get(i) for i in self.y_list.curselection()]
        kind = self.chart_type.get()
        x_col = self.x_var.get()

        if not y_cols:
            messagebox.showinfo("Axe Y manquant", "Sélectionnez au moins une colonne pour l'axe Y.")
            return

        data = self._subset().copy()
        if x_col == INDEX_LABEL:
            x = pd.Series(range(len(data)), index=data.index)
            x_name = "Numéro de mesure"
        else:
            if self.sort_x.get() and kind != "Histogramme":
                data = data.sort_values(x_col)
            x = data[x_col]
            x_name = axis_label([x_col])

        names = {c: describe_column(c)[0] for c in y_cols}

        self.ax.clear()
        self.ax.set_axis_on()

        try:
            if kind == "Histogramme":
                for col in y_cols:
                    self.ax.hist(data[col].dropna(), bins="auto", alpha=0.6, label=names[col])
                self.ax.set_xlabel(axis_label(y_cols))
                self.ax.set_ylabel("Fréquence")
            elif kind == "Barres":
                width = 0.8 / len(y_cols)
                positions = range(len(data))
                for i, col in enumerate(y_cols):
                    self.ax.bar([p + i * width for p in positions], data[col],
                                width=width, label=names[col])
                step = max(1, len(data) // 20)   # évite les étiquettes illisibles
                ticks = list(positions)[::step]
                self.ax.set_xticks([t + width * (len(y_cols) - 1) / 2 for t in ticks])
                self.ax.set_xticklabels([str(v) for v in list(x)[::step]],
                                        rotation=45, ha="right")
                self.ax.set_xlabel(x_name)
            else:
                for col in y_cols:
                    if kind == "Ligne":
                        self.ax.plot(x, data[col], marker="o" if len(data) <= 50 else None,
                                     label=names[col])
                    elif kind == "Nuage de points":
                        self.ax.scatter(x, data[col], s=18, label=names[col])
                    elif kind == "Aire":
                        self.ax.fill_between(x, data[col], alpha=0.4, label=names[col])
                        self.ax.plot(x, data[col])
                self.ax.set_xlabel(x_name)
                if not pd.api.types.is_numeric_dtype(x):
                    self.fig.autofmt_xdate()

            if kind != "Histogramme":
                self.ax.set_ylabel(axis_label(y_cols))
            if len(y_cols) > 1:
                self.ax.legend()
            self.ax.grid(self.grid_on.get(), alpha=0.3)
            title = ", ".join(names[c] for c in y_cols)
            if kind == "Histogramme":
                title = f"Distribution : {title}"
            elif x_col != INDEX_LABEL:
                title = f"{title} selon {describe_column(x_col)[0].lower()}"
            self.ax.set_title(title, wrap=True)
            self.fig.tight_layout()
        except Exception as e:
            self._placeholder("Impossible de tracer ces colonnes")
            messagebox.showerror("Erreur de tracé", str(e))
            return

        self.canvas.draw()

    def save_png(self):
        path = filedialog.asksaveasfilename(defaultextension=".png",
                                            filetypes=[("Image PNG", "*.png")])
        if path:
            self.fig.savefig(path, dpi=150, bbox_inches="tight")
            messagebox.showinfo("Enregistré", f"Graphique enregistré :\n{path}")


if __name__ == "__main__":
    app = JsonGraphApp(sys.argv[1] if len(sys.argv) > 1 else None)
    app.mainloop()
    sys.exit(0)
