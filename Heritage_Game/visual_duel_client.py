#!/usr/bin/env python3
import json
import queue
import random
import socket
import threading
import tkinter as tk
from tkinter import ttk
from tkinter.scrolledtext import ScrolledText


STAT_ORDER = ("attack", "defense", "life", "evasion")
STAT_LABELS = {
    "attack": "ATK",
    "defense": "DEF",
    "life": "LIFE",
    "evasion": "EVA",
}


class VisualDuelClient:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("Heritage Duel Client")
        self.root.geometry("1320x860")
        self.root.configure(bg="#11161d")

        self.socket = None
        self.reader_thread = None
        self.reader_queue: queue.Queue[tuple[str, object]] = queue.Queue()
        self.connected = False
        self.awaiting_name_prompt = False
        self.local_name = ""
        self.self_state = None
        self.enemy_state = None
        self.latest_prompt = "Connect to start a duel."
        self.latest_round = "No round started"
        self.latest_duel = "No duel active"
        self.reveal_text = "No reveal yet"
        self.duel_matchup = "Waiting for round start"
        self.upgrade_phase_active = False
        self.selected_upgrade_slot = 1
        self.upgrade_scales: dict[str, tk.Scale] = {}
        self.upgrade_value_labels: dict[str, tk.Label] = {}
        self.upgrade_reference_labels: dict[str, tk.Label] = {}
        self.upgrade_gauges: dict[str, tk.Canvas] = {}
        self.upgrade_prompt_label = None
        self.upgrade_budget_label = None
        self.upgrade_target_label = None
        self._updating_upgrade_widgets = False

        self._build_styles()
        self._build_ui()
        self.root.after(100, self._drain_queue)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_styles(self) -> None:
        style = ttk.Style()
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("App.TFrame", background="#11161d")
        style.configure("Panel.TFrame", background="#18222d")
        style.configure("Card.TFrame", background="#223140")
        style.configure("Header.TLabel", background="#11161d", foreground="#f2f4f7", font=("Helvetica", 18, "bold"))
        style.configure("Section.TLabel", background="#18222d", foreground="#f2f4f7", font=("Helvetica", 12, "bold"))
        style.configure("Body.TLabel", background="#18222d", foreground="#d9e2ec", font=("Helvetica", 10))
        style.configure("CardTitle.TLabel", background="#223140", foreground="#fff3d6", font=("Helvetica", 11, "bold"))
        style.configure("CardText.TLabel", background="#223140", foreground="#dde7f0", font=("Helvetica", 9))
        style.configure("TopValue.TLabel", background="#223140", foreground="#ffcf5c", font=("Helvetica", 9, "bold"))
        style.configure("Accent.TButton", font=("Helvetica", 10, "bold"))
        style.configure("Stat.TScale", background="#18222d")

    def _build_ui(self) -> None:
        outer = ttk.Frame(self.root, style="App.TFrame", padding=16)
        outer.pack(fill="both", expand=True)

        title = ttk.Label(outer, text="Heritage Duel Visual Client", style="Header.TLabel")
        title.pack(anchor="w")

        subtitle = tk.Label(
            outer,
            text="JSON-driven duel board with readable combat log",
            bg="#11161d",
            fg="#8fa5b8",
            font=("Helvetica", 10),
        )
        subtitle.pack(anchor="w", pady=(2, 12))

        self._build_connection_bar(outer)
        self._build_status_bar(outer)
        self._build_board(outer)
        self._build_upgrade_lab(outer)
        self._build_input_bar(outer)

    def _build_connection_bar(self, parent: ttk.Frame) -> None:
        bar = ttk.Frame(parent, style="Panel.TFrame", padding=12)
        bar.pack(fill="x", pady=(0, 12))

        ttk.Label(bar, text="Host", style="Body.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Label(bar, text="Port", style="Body.TLabel").grid(row=0, column=2, sticky="w", padx=(12, 0))
        ttk.Label(bar, text="Name", style="Body.TLabel").grid(row=0, column=4, sticky="w", padx=(12, 0))

        self.host_var = tk.StringVar(value="127.0.0.1")
        self.port_var = tk.StringVar(value="5050")
        self.name_var = tk.StringVar(value="Player")

        self.host_entry = tk.Entry(bar, textvariable=self.host_var, bg="#0d1319", fg="#f8fbff", insertbackground="#f8fbff", relief="flat")
        self.host_entry.grid(row=0, column=1, sticky="ew", padx=(6, 0))

        self.port_entry = tk.Entry(bar, textvariable=self.port_var, width=8, bg="#0d1319", fg="#f8fbff", insertbackground="#f8fbff", relief="flat")
        self.port_entry.grid(row=0, column=3, sticky="ew", padx=(6, 0))

        self.name_entry = tk.Entry(bar, textvariable=self.name_var, bg="#0d1319", fg="#f8fbff", insertbackground="#f8fbff", relief="flat")
        self.name_entry.grid(row=0, column=5, sticky="ew", padx=(6, 0))

        self.connect_button = ttk.Button(bar, text="Connect", style="Accent.TButton", command=self.connect)
        self.connect_button.grid(row=0, column=6, padx=(16, 8))

        self.disconnect_button = ttk.Button(bar, text="Disconnect", command=self.disconnect)
        self.disconnect_button.grid(row=0, column=7)

        bar.columnconfigure(1, weight=1)
        bar.columnconfigure(5, weight=1)

    def _build_status_bar(self, parent: ttk.Frame) -> None:
        status_wrap = ttk.Frame(parent, style="App.TFrame")
        status_wrap.pack(fill="x", pady=(0, 12))

        self.status_cards = []
        labels = [
            ("Prompt", lambda: self.latest_prompt),
            ("Round", lambda: self.latest_round),
            ("Duel", lambda: self.latest_duel),
            ("Reveal", lambda: self.reveal_text),
        ]
        for index, (title, getter) in enumerate(labels):
            frame = tk.Frame(status_wrap, bg="#18222d", padx=12, pady=10, highlightthickness=1, highlightbackground="#2c3b4a")
            frame.grid(row=0, column=index, sticky="nsew", padx=(0 if index == 0 else 8, 0))
            tk.Label(frame, text=title, bg="#18222d", fg="#8ecae6", font=("Helvetica", 10, "bold")).pack(anchor="w")
            value = tk.Label(frame, text=getter(), bg="#18222d", fg="#f2f4f7", font=("Helvetica", 10), wraplength=280, justify="left")
            value.pack(anchor="w", pady=(4, 0))
            self.status_cards.append((value, getter))
            status_wrap.columnconfigure(index, weight=1)

    def _build_board(self, parent: ttk.Frame) -> None:
        board = ttk.Frame(parent, style="App.TFrame")
        board.pack(fill="both", expand=True)

        left = ttk.Frame(board, style="Panel.TFrame", padding=12)
        center = ttk.Frame(board, style="Panel.TFrame", padding=12)
        right = ttk.Frame(board, style="Panel.TFrame", padding=12)

        left.grid(row=0, column=0, sticky="nsew")
        center.grid(row=0, column=1, sticky="nsew", padx=12)
        right.grid(row=0, column=2, sticky="nsew")

        board.columnconfigure(0, weight=3)
        board.columnconfigure(1, weight=4)
        board.columnconfigure(2, weight=3)
        board.rowconfigure(0, weight=1)

        ttk.Label(left, text="Your Party", style="Section.TLabel").pack(anchor="w")
        self.self_party_frame = tk.Frame(left, bg="#18222d")
        self.self_party_frame.pack(fill="both", expand=True, pady=(10, 0))

        ttk.Label(center, text="Battle Feed", style="Section.TLabel").pack(anchor="w")
        self.duel_banner = tk.Label(center, text=self.duel_matchup, bg="#18222d", fg="#fff3d6", font=("Helvetica", 13, "bold"), wraplength=500, justify="left")
        self.duel_banner.pack(anchor="w", pady=(8, 10))

        self.log = ScrolledText(
            center,
            height=24,
            bg="#0f151c",
            fg="#dce6ef",
            insertbackground="#ffffff",
            relief="flat",
            wrap="word",
            font=("Consolas", 10),
        )
        self.log.pack(fill="both", expand=True)
        self.log.configure(state="disabled")

        ttk.Label(right, text="Opponent Party", style="Section.TLabel").pack(anchor="w")
        self.enemy_party_frame = tk.Frame(right, bg="#18222d")
        self.enemy_party_frame.pack(fill="both", expand=True, pady=(10, 0))

    def _build_input_bar(self, parent: ttk.Frame) -> None:
        bar = ttk.Frame(parent, style="Panel.TFrame", padding=12)
        bar.pack(fill="x", pady=(12, 0))

        self.input_var = tk.StringVar()
        self.input_entry = tk.Entry(bar, textvariable=self.input_var, bg="#0d1319", fg="#f8fbff", insertbackground="#f8fbff", relief="flat", font=("Helvetica", 11))
        self.input_entry.pack(side="left", fill="x", expand=True)
        self.input_entry.bind("<Return>", lambda _event: self.send_input())

        self.suggest_button = ttk.Button(bar, text="Suggest 3", command=self._suggest_best_lineup)
        self.suggest_button.pack(side="left", padx=(10, 0))

        self.auto_lineup_button = ttk.Button(bar, text="Auto Best 3", style="Accent.TButton", command=self._send_best_lineup)
        self.auto_lineup_button.pack(side="left", padx=(8, 0))

        self.send_button = ttk.Button(bar, text="Send", style="Accent.TButton", command=self.send_input)
        self.send_button.pack(side="left", padx=(8, 0))

    def _build_upgrade_lab(self, parent: ttk.Frame) -> None:
        lab = ttk.Frame(parent, style="Panel.TFrame", padding=12)
        lab.pack(fill="x", pady=(12, 0))

        title_row = tk.Frame(lab, bg="#18222d")
        title_row.pack(fill="x")
        tk.Label(title_row, text="Upgrade Lab", bg="#18222d", fg="#fff3d6", font=("Helvetica", 12, "bold")).pack(anchor="w")
        self.upgrade_prompt_label = tk.Label(
            title_row,
            text="Pick a creature to shape its build.",
            bg="#18222d",
            fg="#9db1c3",
            font=("Helvetica", 9),
        )
        self.upgrade_prompt_label.pack(anchor="w", pady=(2, 0))

        summary_row = tk.Frame(lab, bg="#18222d")
        summary_row.pack(fill="x", pady=(10, 8))

        self.upgrade_target_label = tk.Label(
            summary_row,
            text="Selected creature: none",
            bg="#18222d",
            fg="#8ecae6",
            font=("Helvetica", 10, "bold"),
        )
        self.upgrade_target_label.pack(side="left")

        self.upgrade_budget_label = tk.Label(
            summary_row,
            text="Remaining points: 0",
            bg="#18222d",
            fg="#ffcf5c",
            font=("Helvetica", 10, "bold"),
        )
        self.upgrade_budget_label.pack(side="right")

        fader_row = tk.Frame(lab, bg="#18222d")
        fader_row.pack(fill="x")

        for index, stat in enumerate(STAT_ORDER):
            column = tk.Frame(fader_row, bg="#223140", padx=10, pady=10, highlightthickness=1, highlightbackground="#344b61")
            column.grid(row=0, column=index, sticky="nsew", padx=(0 if index == 0 else 8, 0))
            fader_row.columnconfigure(index, weight=1)

            tk.Label(column, text=STAT_LABELS[stat], bg="#223140", fg="#fff3d6", font=("Helvetica", 10, "bold")).pack(anchor="center")

            gauge = tk.Canvas(column, width=88, height=132, bg="#223140", highlightthickness=0)
            gauge.pack(pady=(8, 6))
            self.upgrade_gauges[stat] = gauge

            scale = tk.Scale(
                column,
                from_=0,
                to=0,
                orient="vertical",
                showvalue=False,
                resolution=1,
                length=150,
                troughcolor="#0d1319",
                highlightthickness=0,
                bg="#223140",
                fg="#dce6ef",
                activebackground="#8ecae6",
                command=lambda value, key=stat: self._on_upgrade_slider_change(key, value),
            )
            scale.pack()
            self.upgrade_scales[stat] = scale

            value_label = tk.Label(column, text="0", bg="#223140", fg="#dce6ef", font=("Helvetica", 9, "bold"))
            value_label.pack(pady=(6, 0))
            self.upgrade_value_labels[stat] = value_label

            reference_label = tk.Label(column, text="AVG 0.0", bg="#223140", fg="#9bc1d9", font=("Helvetica", 8))
            reference_label.pack()
            self.upgrade_reference_labels[stat] = reference_label

        action_row = tk.Frame(lab, bg="#18222d")
        action_row.pack(fill="x", pady=(10, 0))

        ttk.Button(action_row, text="Balanced", command=self._apply_balanced_preset).pack(side="left")
        ttk.Button(action_row, text="Offense", command=self._apply_offense_preset).pack(side="left", padx=(8, 0))
        ttk.Button(action_row, text="Defense", command=self._apply_defense_preset).pack(side="left", padx=(8, 0))
        ttk.Button(action_row, text="Random", command=self._apply_random_preset).pack(side="left", padx=(8, 0))
        ttk.Button(action_row, text="Smart", command=self._apply_smart_preset).pack(side="left", padx=(8, 0))
        ttk.Button(action_row, text="Auto Pick", command=self._auto_pick_creature).pack(side="left", padx=(8, 0))
        ttk.Button(action_row, text="Clear", command=self._clear_upgrade_plan).pack(side="left", padx=(8, 0))
        ttk.Button(action_row, text="Apply Plan", style="Accent.TButton", command=self._apply_upgrade_plan).pack(side="right")

    def _selected_party_slot(self) -> int | None:
        if not self.self_state:
            return None
        party = self.self_state.get("party", [])
        if not isinstance(party, list):
            return None
        previous_slot = self.selected_upgrade_slot
        for creature in party:
            if isinstance(creature, dict) and creature.get("slot") == self.selected_upgrade_slot:
                return self.selected_upgrade_slot
        if party:
            first = party[0]
            if isinstance(first, dict):
                self.selected_upgrade_slot = int(first.get("slot", 1))
                if self.selected_upgrade_slot != previous_slot:
                    self._clear_upgrade_plan()
                return self.selected_upgrade_slot
        return None

    def _selected_creature(self) -> dict | None:
        if not self.self_state:
            return None
        party = self.self_state.get("party", [])
        if not isinstance(party, list):
            return None
        for creature in party:
            if isinstance(creature, dict) and creature.get("slot") == self.selected_upgrade_slot:
                return creature
        return None

    def _selected_upgrade_budget(self) -> int:
        if not self.self_state:
            return 0
        try:
            return int(self.self_state.get("unspentUpgradePoints", 0))
        except (TypeError, ValueError):
            return 0

    def _selected_reference_value(self, creature: dict | None, stat: str) -> float:
        if not creature:
            return 0.0
        reference = creature.get("reference")
        if isinstance(reference, dict):
            value = reference.get(stat)
            if isinstance(value, (int, float)):
                return float(value)
        value = creature.get(stat)
        if isinstance(value, (int, float)):
            return float(value)
        return 0.0

    def _selected_current_value(self, creature: dict | None, stat: str) -> float:
        if not creature:
            return 0.0
        value = creature.get(stat)
        if isinstance(value, (int, float)):
            return float(value)
        return 0.0

    def _creature_role_hint(self, creature: dict | None) -> str:
        if not creature:
            return ""
        archetype = str(creature.get("archetype", "")).lower()
        if "ranger" in archetype and "tank" in archetype:
            return "skirmisher"
        if "warrior" in archetype and "tank" in archetype:
            return "bulwark"
        if "ranger" in archetype and "warrior" in archetype:
            return "duelist"
        if "ranger" in archetype:
            return "ranger"
        if "warrior" in archetype:
            return "warrior"
        if "tank" in archetype:
            return "tank"
        return "mixed"

    def _set_plan_from_weights(self, weights: dict[str, float]) -> None:
        budget = self._selected_upgrade_budget()
        if budget <= 0:
            return

        clamped_weights = {stat: max(0.0, float(weights.get(stat, 0.0))) for stat in STAT_ORDER}
        total_weight = sum(clamped_weights.values())
        if total_weight <= 0.0:
            self._apply_balanced_preset()
            return

        assigned = {stat: int(budget * (clamped_weights[stat] / total_weight)) for stat in STAT_ORDER}
        remainder = budget - sum(assigned.values())
        order = sorted(STAT_ORDER, key=lambda stat: (clamped_weights[stat], random.random()), reverse=True)
        for index in range(remainder):
            assigned[order[index % len(order)]] += 1

        for stat in STAT_ORDER:
            self._set_upgrade_scale(stat, assigned[stat])
        self._refresh_upgrade_lab()

    def _render_upgrade_gauge(self, stat: str, value: float, reference: float) -> None:
        canvas = self.upgrade_gauges.get(stat)
        if canvas is None:
            return
        canvas.delete("all")

        width = 88
        height = 132
        center_x = width / 2
        track_top = 10
        track_bottom = height - 18
        track_height = track_bottom - track_top
        max_value = max(reference * 1.5, value, 1.0)
        reference_y = track_bottom - (reference / max_value) * track_height
        value_y = track_bottom - (value / max_value) * track_height

        canvas.create_rectangle(center_x - 9, track_top, center_x + 9, track_bottom, fill="#0d1319", outline="#344b61")
        canvas.create_line(14, reference_y, width - 14, reference_y, fill="#ffcf5c", width=2)
        canvas.create_text(width / 2, max(8, reference_y - 10), text="AVG", fill="#ffcf5c", font=("Helvetica", 8, "bold"))
        canvas.create_rectangle(center_x - 9, value_y, center_x + 9, track_bottom, fill="#8ecae6", outline="")
        canvas.create_oval(center_x - 14, value_y - 6, center_x + 14, value_y + 6, fill="#dce6ef", outline="#f8fbff")

    def _refresh_upgrade_lab(self) -> None:
        creature = self._selected_creature()
        budget = self._selected_upgrade_budget()
        active_state = "normal" if self.upgrade_phase_active and creature and budget > 0 else "disabled"
        if self.upgrade_target_label is not None:
            if creature:
                name = creature.get("name", "Unknown")
                archetype = creature.get("archetype", "Unknown")
                self.upgrade_target_label.configure(text=f"Selected creature: {name} [{archetype}] (slot {self.selected_upgrade_slot})")
            else:
                self.upgrade_target_label.configure(text="Selected creature: none")
        if self.upgrade_budget_label is not None:
            self.upgrade_budget_label.configure(text=f"Remaining points: {budget}")
        if self.upgrade_prompt_label is not None:
            if self.upgrade_phase_active:
                self.upgrade_prompt_label.configure(text="Drag the four faders, then apply the plan to spend the remaining points.")
            else:
                self.upgrade_prompt_label.configure(text="Upgrade phase is inactive. The lab will activate when points can be spent.")

        for stat in STAT_ORDER:
            reference = self._selected_reference_value(creature, stat)
            current = self._selected_current_value(creature, stat)
            scale = self.upgrade_scales.get(stat)
            if scale is not None:
                scale.configure(to=max(0, budget), state=active_state)
                if int(scale.get()) > budget:
                    self._set_upgrade_scale(stat, budget)
            planned = int(scale.get()) if scale is not None else 0
            if self.upgrade_value_labels.get(stat) is not None:
                self.upgrade_value_labels[stat].configure(text=f"{STAT_LABELS[stat]} +{planned}")
            if self.upgrade_reference_labels.get(stat) is not None:
                self.upgrade_reference_labels[stat].configure(text=f"AVG {reference:.1f} | now {current:.1f}")
            self._render_upgrade_gauge(stat, current + planned, reference)

    def _set_upgrade_scale(self, stat: str, value: int) -> None:
        scale = self.upgrade_scales.get(stat)
        if scale is None:
            return
        self._updating_upgrade_widgets = True
        try:
            scale.set(max(0, int(value)))
        finally:
            self._updating_upgrade_widgets = False

    def _clear_upgrade_plan(self) -> None:
        for stat in STAT_ORDER:
            self._set_upgrade_scale(stat, 0)
        self._refresh_upgrade_lab()

    def _apply_balanced_preset(self) -> None:
        budget = self._selected_upgrade_budget()
        if budget <= 0:
            return
        base = budget // len(STAT_ORDER)
        remainder = budget % len(STAT_ORDER)
        for index, stat in enumerate(STAT_ORDER):
            self._set_upgrade_scale(stat, base + (1 if index < remainder else 0))
        self._refresh_upgrade_lab()

    def _apply_offense_preset(self) -> None:
        budget = self._selected_upgrade_budget()
        if budget <= 0:
            return
        plan = {"attack": int(budget * 0.5), "defense": int(budget * 0.15), "life": int(budget * 0.2)}
        plan["evasion"] = max(0, budget - sum(plan.values()))
        for stat in STAT_ORDER:
            self._set_upgrade_scale(stat, plan.get(stat, 0))
        self._refresh_upgrade_lab()

    def _apply_defense_preset(self) -> None:
        budget = self._selected_upgrade_budget()
        if budget <= 0:
            return
        plan = {"attack": int(budget * 0.1), "defense": int(budget * 0.35), "life": int(budget * 0.4)}
        plan["evasion"] = max(0, budget - sum(plan.values()))
        for stat in STAT_ORDER:
            self._set_upgrade_scale(stat, plan.get(stat, 0))
        self._refresh_upgrade_lab()

    def _apply_random_preset(self) -> None:
        budget = self._selected_upgrade_budget()
        if budget <= 0:
            return

        plan = {stat: 0 for stat in STAT_ORDER}
        for _ in range(budget):
            plan[random.choice(STAT_ORDER)] += 1

        for stat in STAT_ORDER:
            self._set_upgrade_scale(stat, plan[stat])
        self._refresh_upgrade_lab()

    def _apply_smart_preset(self) -> None:
        creature = self._selected_creature()
        role = self._creature_role_hint(creature)

        if role == "ranger":
            weights = {"attack": 5.0, "evasion": 4.0, "life": 1.0, "defense": 1.0}
        elif role == "warrior":
            weights = {"attack": 4.0, "defense": 4.0, "life": 2.0, "evasion": 1.0}
        elif role == "tank":
            weights = {"defense": 5.0, "life": 4.0, "attack": 1.0, "evasion": 1.0}
        elif role == "skirmisher":
            weights = {"attack": 4.0, "evasion": 4.0, "life": 1.5, "defense": 1.5}
        elif role == "bulwark":
            weights = {"defense": 5.0, "life": 3.5, "attack": 1.0, "evasion": 0.5}
        elif role == "duelist":
            weights = {"attack": 4.0, "defense": 2.0, "evasion": 3.0, "life": 1.0}
        else:
            weights = {"attack": 3.0, "defense": 2.5, "life": 2.5, "evasion": 2.0}

        self._set_plan_from_weights(weights)

    def _auto_pick_creature(self) -> None:
        if not self.self_state:
            return
        party = self.self_state.get("party", [])
        if not isinstance(party, list) or not party:
            return

        budget = self._selected_upgrade_budget()

        def score(creature: dict) -> float:
            attack = self._selected_current_value(creature, "attack")
            defense = self._selected_current_value(creature, "defense")
            life = self._selected_current_value(creature, "life")
            evasion = self._selected_current_value(creature, "evasion")
            archetype = str(creature.get("archetype", "")).lower()

            if "ranger" in archetype and "tank" in archetype:
                role_bias = 6.0
            elif "warrior" in archetype and "tank" in archetype:
                role_bias = 5.0
            elif "ranger" in archetype and "warrior" in archetype:
                role_bias = 4.5
            elif "ranger" in archetype:
                role_bias = 4.0
            elif "warrior" in archetype:
                role_bias = 3.5
            elif "tank" in archetype:
                role_bias = 3.0
            else:
                role_bias = 2.0

            durability = defense * 0.9 + life * 0.6 + evasion * 9.0
            pressure = attack * 1.1
            point_sense = budget * 0.05 if budget > 0 else 0.0
            return pressure + durability + role_bias + point_sense

        best = max(party, key=score)
        slot = best.get("slot")
        if isinstance(slot, int):
            self._select_upgrade_slot(slot)

    def _party_creatures(self) -> list[dict]:
        if not self.self_state:
            return []
        party = self.self_state.get("party", [])
        if not isinstance(party, list):
            return []
        return [creature for creature in party if isinstance(creature, dict)]

    def _lineup_score(self, creature: dict) -> float:
        attack = self._selected_current_value(creature, "attack")
        defense = self._selected_current_value(creature, "defense")
        life = self._selected_current_value(creature, "life")
        evasion = self._selected_current_value(creature, "evasion")
        archetype = str(creature.get("archetype", "")).lower()

        if "ranger" in archetype and "tank" in archetype:
            role_bias = 5.5
        elif "warrior" in archetype and "tank" in archetype:
            role_bias = 5.0
        elif "ranger" in archetype and "warrior" in archetype:
            role_bias = 4.8
        elif "ranger" in archetype:
            role_bias = 4.0
        elif "warrior" in archetype:
            role_bias = 3.6
        elif "tank" in archetype:
            role_bias = 3.2
        else:
            role_bias = 2.0

        stability = defense * 0.85 + life * 0.55 + evasion * 10.0
        pressure = attack * 1.15
        return pressure + stability + role_bias

    def _best_lineup(self) -> tuple[list[int], int | None]:
        party = self._party_creatures()
        if len(party) < 3:
            return [], None

        ranked = sorted(party, key=self._lineup_score, reverse=True)
        best_three = ranked[:3]
        backup = ranked[3] if len(ranked) > 3 else None

        slots: list[int] = []
        for creature in best_three:
            slot = creature.get("slot")
            if isinstance(slot, int):
                slots.append(slot)

        backup_slot = None
        if isinstance(backup, dict):
            slot = backup.get("slot")
            if isinstance(slot, int):
                backup_slot = slot

        return slots, backup_slot

    def _format_lineup_command(self, slots: list[int]) -> str:
        return " ".join(str(slot) for slot in slots)

    def _suggest_best_lineup(self) -> None:
        slots, backup = self._best_lineup()
        if len(slots) != 3:
            self.append_log("Not enough creatures to suggest a lineup.", accent=True)
            return
        command = self._format_lineup_command(slots)
        self.input_var.set(command)
        if backup is not None:
            self.append_log(f"Suggested best 3: {command} | backup: {backup}", accent=True)
        else:
            self.append_log(f"Suggested best 3: {command}", accent=True)

    def _send_best_lineup(self) -> None:
        slots, backup = self._best_lineup()
        if len(slots) != 3:
            self.append_log("Not enough creatures to auto-pick a lineup.", accent=True)
            return
        command = self._format_lineup_command(slots)
        self.input_var.set(command)
        self.send_input()
        if backup is not None:
            self.append_log(f"Auto-picked lineup {command}; backup slot {backup} is left for tactical swaps.", accent=True)
        else:
            self.append_log(f"Auto-picked lineup {command}.", accent=True)

    def _apply_upgrade_plan(self) -> None:
        if not self.connected or self.socket is None:
            return
        slot = self._selected_party_slot()
        if slot is None:
            self.append_log("No creature selected for upgrades.", accent=True)
            return

        budget = self._selected_upgrade_budget()
        planned: dict[str, int] = {}
        total = 0
        for stat in STAT_ORDER:
            value = max(0, int(self.upgrade_scales[stat].get()))
            planned[stat] = value
            total += value

        if total <= 0:
            self.append_log("Upgrade plan is empty.", accent=True)
            return
        if total > budget:
            self.append_log(f"Plan exceeds remaining points ({total} > {budget}).", accent=True)
            return

        commands = [f"{slot} {stat_map} {planned[stat_map]}" for stat_map in STAT_ORDER if planned[stat_map] > 0]
        if not commands:
            self.append_log("Nothing to apply.", accent=True)
            return

        try:
            self.socket.sendall(("\n".join(commands) + "\n").encode("utf-8"))
        except OSError as exc:
            self.append_log(f"Apply failed: {exc}", accent=True)
            self.disconnect()
            return

        self.append_log(f"Applied upgrade plan to slot {slot}: " + ", ".join(commands), accent=True)
        self._clear_upgrade_plan()

    def _on_upgrade_slider_change(self, stat: str, raw_value: str) -> None:
        if self._updating_upgrade_widgets:
            return

        budget = self._selected_upgrade_budget()
        if budget <= 0:
            self._set_upgrade_scale(stat, 0)
            self._refresh_upgrade_lab()
            return

        try:
            requested = max(0, int(float(raw_value)))
        except ValueError:
            requested = 0

        other_total = 0
        for other_stat in STAT_ORDER:
            if other_stat == stat:
                continue
            other_total += int(self.upgrade_scales[other_stat].get())

        allowed = max(0, budget - other_total)
        if requested > allowed:
            requested = allowed
            self._set_upgrade_scale(stat, requested)

        self._refresh_upgrade_lab()

    def append_log(self, text: str, accent: bool = False) -> None:
        self.log.configure(state="normal")
        if accent:
            self.log.insert("end", text + "\n", "accent")
            self.log.tag_configure("accent", foreground="#ffcf5c")
        else:
            self.log.insert("end", text + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def connect(self) -> None:
        if self.connected:
            return
        host = self.host_var.get().strip() or "127.0.0.1"
        port_text = self.port_var.get().strip()
        name = self.name_var.get().strip() or "Player"
        try:
            port = int(port_text)
        except ValueError:
            self.append_log("Invalid port.", accent=True)
            return

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.connect((host, port))
        except OSError as exc:
            self.append_log(f"Connection failed: {exc}", accent=True)
            sock.close()
            return

        self.socket = sock
        self.connected = True
        self.local_name = name
        self.awaiting_name_prompt = True
        self.append_log(f"Connected to {host}:{port} as {name}", accent=True)
        self.reader_thread = threading.Thread(target=self._socket_reader, daemon=True)
        self.reader_thread.start()
        self.latest_prompt = "Waiting for server prompt..."
        self._refresh_status_cards()
        self.input_entry.focus_set()

    def disconnect(self) -> None:
        if self.socket is not None:
            try:
                self.socket.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                self.socket.close()
            except OSError:
                pass
        self.socket = None
        self.connected = False
        self.append_log("Disconnected.", accent=True)
        self.latest_prompt = "Disconnected"
        self._refresh_status_cards()

    def send_input(self) -> None:
        if not self.connected or self.socket is None:
            return
        text = self.input_var.get().strip()
        if not text:
            return
        try:
            self.socket.sendall((text + "\n").encode("utf-8"))
        except OSError as exc:
            self.append_log(f"Send failed: {exc}", accent=True)
            self.disconnect()
            return
        self.append_log(f"> {text}", accent=True)
        self.input_var.set("")

    def _socket_reader(self) -> None:
        assert self.socket is not None
        sock = self.socket
        file_obj = sock.makefile("r", encoding="utf-8", newline="\n")
        try:
            for raw_line in file_obj:
                line = raw_line.rstrip("\n")
                if line.startswith("@event "):
                    payload = line[len("@event "):]
                    try:
                        event = json.loads(payload)
                    except json.JSONDecodeError as exc:
                        self.reader_queue.put(("line", f"Invalid event JSON: {exc}"))
                        continue
                    self.reader_queue.put(("event", event))
                else:
                    self.reader_queue.put(("line", line))
        except OSError as exc:
            self.reader_queue.put(("line", f"Connection error: {exc}"))
        finally:
            self.reader_queue.put(("disconnect", None))
            try:
                file_obj.close()
            except OSError:
                pass

    def _drain_queue(self) -> None:
        while True:
            try:
                kind, payload = self.reader_queue.get_nowait()
            except queue.Empty:
                break

            if kind == "line":
                line = str(payload)
                self._handle_text_line(line)
            elif kind == "event":
                self._handle_event(payload)
            elif kind == "disconnect":
                self.connected = False
                if self.socket is not None:
                    try:
                        self.socket.close()
                    except OSError:
                        pass
                    self.socket = None
                self.latest_prompt = "Connection closed"
                self._refresh_status_cards()

        self.root.after(100, self._drain_queue)

    def _handle_text_line(self, line: str) -> None:
        if not line:
            return
        self.append_log(line)
        lowered = line.lower()
        if self.awaiting_name_prompt and "enter your display name" in lowered and self.socket is not None:
            try:
                self.socket.sendall((self.local_name + "\n").encode("utf-8"))
                self.append_log(f"> {self.local_name}", accent=True)
                self.awaiting_name_prompt = False
            except OSError as exc:
                self.append_log(f"Auto-name send failed: {exc}", accent=True)
                self.disconnect()
                return
        if any(token in lowered for token in ("pick ", "enter lineup indices", "spend points", "type 'done'", "invalid")):
            self.latest_prompt = line
            self._refresh_status_cards()
        if line.startswith("Duel "):
            self.duel_matchup = line
            self.latest_duel = line
            self.duel_banner.configure(text=self.duel_matchup)
            self._refresh_status_cards()
        if line.startswith("Revealed enemy types:"):
            self.reveal_text = line.replace("Revealed enemy types:", "").strip()
            self._refresh_status_cards()
        if line.startswith("=== Round "):
            self.latest_round = line.replace("=== ", "").replace(" ===", "")
            self._refresh_status_cards()

    def _handle_event(self, event: dict) -> None:
        event_type = event.get("type", "")

        if event_type == "player_connected":
            if event.get("playerName") == self.local_name:
                self.append_log(f"You are registered as {self.local_name}", accent=True)

        elif event_type == "party_state":
            player = event.get("player")
            if isinstance(player, dict):
                if player.get("name") == self.local_name:
                    self.self_state = player
                    self._selected_party_slot()
                else:
                    self.enemy_state = player
                self._render_party_panels()
                self._refresh_upgrade_lab()

        elif event_type == "prompt":
            phase = event.get("phase", "input")
            self.latest_prompt = f"Prompt: {phase}"
            self.upgrade_phase_active = phase == "upgrade"
            self._refresh_upgrade_lab()
            self._refresh_status_cards()

        elif event_type == "upgrade_phase_started":
            self.upgrade_phase_active = True
            self._refresh_upgrade_lab()

        elif event_type == "upgrade_phase_finished":
            self.upgrade_phase_active = False
            self._refresh_upgrade_lab()

        elif event_type == "round_started":
            round_number = event.get("round", "?")
            first_picker = event.get("firstPicker", "?")
            self.latest_round = f"Round {round_number}"
            self.latest_prompt = f"{first_picker} has initiative"
            self._refresh_status_cards()

        elif event_type == "lineup_reveal":
            revealed = event.get("revealedTypes", [])
            if isinstance(revealed, list):
                self.reveal_text = ", ".join(str(item) for item in revealed)
                self._refresh_status_cards()

        elif event_type == "duel_started":
            p1 = event.get("playerOne", {})
            p2 = event.get("playerTwo", {})
            duel_number = event.get("duel", "?")
            self.latest_duel = f"Duel {duel_number}"
            self.duel_matchup = f"{p1.get('name', 'Unknown')} vs {p2.get('name', 'Unknown')}"
            self.duel_banner.configure(text=self.duel_matchup)
            self._refresh_status_cards()

        elif event_type == "duel_resolved":
            duel_number = event.get("duel", "?")
            winner = event.get("winner", 0)
            hp_one = event.get("hpOne", 0)
            hp_two = event.get("hpTwo", 0)
            self.latest_duel = f"Duel {duel_number} resolved"
            self.duel_banner.configure(text=f"Duel {duel_number} result | HP {hp_one} - {hp_two} | winner {winner}")
            self._refresh_status_cards()

        elif event_type == "level_up":
            self.append_log(
                f"Level up: {event.get('playerName')} reached level {event.get('level')} and gained {event.get('pointsGained')} upgrade points.",
                accent=True,
            )

        elif event_type == "match_resolved":
            self.latest_prompt = "Match complete"
            self.upgrade_phase_active = False
            self._refresh_upgrade_lab()
            self._refresh_status_cards()

    def _render_party_panels(self) -> None:
        self._render_party(self.self_party_frame, self.self_state, own=True)
        self._render_party(self.enemy_party_frame, self.enemy_state, own=False)

    def _render_party(self, container: tk.Frame, state: dict | None, own: bool) -> None:
        for child in container.winfo_children():
            child.destroy()

        if not state:
            tk.Label(container, text="Waiting for party data...", bg="#18222d", fg="#9db1c3", font=("Helvetica", 10)).pack(anchor="w")
            return

        header = tk.Label(
            container,
            text=f"{state.get('name', 'Unknown')} | level {state.get('level', 1)} | xp {state.get('xp', 0)} | unspent {state.get('unspentUpgradePoints', 0)}",
            bg="#18222d",
            fg="#8ecae6" if own else "#ffb4a2",
            font=("Helvetica", 11, "bold"),
            wraplength=360,
            justify="left",
        )
        header.pack(anchor="w", pady=(0, 8))

        for creature in state.get("party", []):
            card = tk.Frame(container, bg="#223140", padx=10, pady=8, highlightthickness=1, highlightbackground="#344b61")
            card.pack(fill="x", pady=(0, 8))

            if own and creature.get("slot") == self.selected_upgrade_slot:
                card.configure(highlightbackground="#8ecae6")

            if own:
                slot_value = creature.get("slot")
                card.bind("<Button-1>", lambda _event, slot=slot_value: self._select_upgrade_slot(slot))

            title = f"{creature.get('slot')} · {creature.get('name')}"
            subtitle = creature.get("archetype", "Unknown")
            title_label = tk.Label(card, text=title, bg="#223140", fg="#fff3d6", font=("Helvetica", 10, "bold"), anchor="w")
            title_label.pack(fill="x")
            subtitle_label = tk.Label(card, text=subtitle, bg="#223140", fg="#9bc1d9", font=("Helvetica", 9), anchor="w")
            subtitle_label.pack(fill="x", pady=(0, 6))

            if own:
                title_label.bind("<Button-1>", lambda _event, slot=creature.get("slot"): self._select_upgrade_slot(slot))
                subtitle_label.bind("<Button-1>", lambda _event, slot=creature.get("slot"): self._select_upgrade_slot(slot))

            stats = [
                ("ATK", creature.get("attack"), creature.get("top10", {}).get("attack")),
                ("DEF", creature.get("defense"), creature.get("top10", {}).get("defense")),
                ("EVA", creature.get("evasion"), creature.get("top10", {}).get("evasion")),
                ("LIFE", creature.get("life"), creature.get("top10", {}).get("life")),
            ]
            stat_row = tk.Frame(card, bg="#223140")
            stat_row.pack(fill="x")
            for idx, (label, value, top10) in enumerate(stats):
                pill = tk.Frame(stat_row, bg="#182430" if not top10 else "#604a0d", padx=8, pady=5)
                pill.grid(row=0, column=idx, padx=(0, 6), sticky="w")
                color = "#d9e2ec" if not top10 else "#ffcf5c"
                tk.Label(pill, text=f"{label} {value}", bg=pill["bg"], fg=color, font=("Helvetica", 9, "bold")).pack()

    def _select_upgrade_slot(self, slot: int | None) -> None:
        if slot is None:
            return
        try:
            slot_value = int(slot)
        except (TypeError, ValueError):
            return
        new_slot = max(1, min(slot_value, 5))
        if new_slot != self.selected_upgrade_slot:
            self.selected_upgrade_slot = new_slot
            self._clear_upgrade_plan()
        else:
            self.selected_upgrade_slot = new_slot
        self._refresh_upgrade_lab()

    def _refresh_status_cards(self) -> None:
        for label, getter in self.status_cards:
            label.configure(text=getter())

    def _on_close(self) -> None:
        self.disconnect()
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    VisualDuelClient(root)
    root.mainloop()


if __name__ == "__main__":
    main()
