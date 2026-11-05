"""
vkEngine Editor Launcher
========================
A standalone GUI launcher for the vkEngine editor.

Provides two workflows:
  1. Open Project  — file dialog to pick an editorconfig.json, then launch editor.exe
  2. Create Project — form to configure project settings, create the project directory
     and initial config files, then launch editor.exe

Usage:
    python tools/editor_launcher.py
"""

import json
import os
import platform
import subprocess
import sys
import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext


def get_repo_root() -> str:
    """Return the repository root (parent of the tools/ directory)."""
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_editor_exe() -> str | None:
    """Locate the editor executable relative to the repository root."""
    repo = get_repo_root()
    exe_name = "editor.exe" if platform.system() == "Windows" else "editor"
    candidate = os.path.join(repo, "out", exe_name)
    return candidate if os.path.isfile(candidate) else None


_editor_process: subprocess.Popen | None = None


def launch_editor(config_path: str) -> subprocess.Popen | None:
    """Launch editor.exe with the given config path.

    Returns the Popen object, or None on failure.
    The editor resolves content from its executable and project config locations.
    """
    global _editor_process

    exe = find_editor_exe()
    if exe is None:
        messagebox.showerror(
            "Editor Not Found",
            f"Could not find the editor executable.\n\n"
            f"Expected at: {os.path.join(get_repo_root(), 'out', 'editor.exe')}\n\n"
            f"Please build the project first by running:\n"
            f"  scons",
        )
        return None

    if not os.path.isfile(config_path):
        messagebox.showerror("Config Not Found",
                             f"Editor config file not found:\n{config_path}")
        return None

    try:
        proc = subprocess.Popen(
            [exe, os.path.abspath(config_path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        _editor_process = proc
        return proc
    except FileNotFoundError:
        messagebox.showerror("Launch Failed",
                             f"Could not launch:\n{exe}")
        return None
    except OSError as e:
        messagebox.showerror("Launch Failed",
                             f"Failed to launch editor:\n{e}")
        return None


def create_project_files(project_dir: str, width: int, height: int) -> None:
    """Create editorconfig.json, gameconfig.json and scene.json inside *project_dir*.

    Paths in generated configuration files are relative to the project root.
    """
    editor_config = {
        "windowWidth": width,
        "windowHeight": height,
        "projectRoot": ".",
        "assetDBPath": "asset_db.db",
        "gameConfigPath": "gameconfig.json",
    }
    with open(os.path.join(project_dir, "editorconfig.json"), "w") as f:
        json.dump(editor_config, f, indent=4)

    game_config = {
        "windowWidth": width,
        "windowHeight": height,
        "enableVulkanValidationLayers": False,
        "assetLUTPath": "asset_db.db",
        "defaultScenePath": "scene.json",
        "gameScriptPath": "",
        "physicsConfig": {},
        "renderConfig": {},
    }
    with open(os.path.join(project_dir, "gameconfig.json"), "w") as f:
        json.dump(game_config, f, indent=4)

    scene = {
        "maxid": 3,
        "objects": [
            {
                "id": 1,
                "static": False,
                "name": "Camera",
                "parent": 0,
                "transform": {
                    "pos": [0.0, 0.0, 5.0],
                    "scl": [1.0, 1.0, 1.0],
                    "rot": [0.0, 0.0, 0.0, 1.0],
                },
                "components": [
                    {
                        "type": "camera",
                        "fov": 60.0,
                        "width": float(width),
                        "height": float(height),
                        "near": 0.01,
                        "far": 1000.0,
                    }
                ],
            },
            {
                "id": 2,
                "static": False,
                "name": "Directional Light",
                "parent": 0,
                "transform": {
                    "pos": [0.0, 0.0, 0.0],
                    "scl": [1.0, 1.0, 1.0],
                    "rot": [-0.6324555, 0.3162278, 0.0, 0.7071068],
                },
                "components": [
                    {
                        "type": "directionalLight",
                        "color": [1.0, 0.96, 0.9],
                        "intensity": 4.0,
                    }
                ],
            },
        ],
    }
    with open(os.path.join(project_dir, "scene.json"), "w") as f:
        json.dump(scene, f, indent=4)


class ConsoleWindow(tk.Toplevel):
    """Read-only window that tails stdout from an editor subprocess."""

    def __init__(self, parent: tk.Tk, proc: subprocess.Popen, title: str):
        super().__init__(parent)
        self.title(title)
        self.geometry("700x400")
        self.proc = proc

        self.text = scrolledtext.ScrolledText(self, state=tk.DISABLED,
                                               wrap=tk.WORD,
                                               font=("Consolas", 10))
        self.text.pack(fill=tk.BOTH, expand=True)

        self.close_btn = tk.Button(self, text="Close (editor keeps running)",
                                   command=self._on_close)
        self.close_btn.pack(pady=4)

        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self._poll()

    def _poll(self) -> None:
        """Read available output from the subprocess and schedule the next poll."""
        if self.proc and self.proc.stdout and not self.proc.poll() is not None:
            import select

            try:
                lines = []
                while True:
                    line = self.proc.stdout.readline()
                    if not line:
                        break
                    lines.append(line.rstrip("\n\r"))
                if lines:
                    self._append("\n".join(lines))
            except Exception:
                pass
            self.after(100, self._poll)
        elif self.proc and self.proc.poll() is not None:
            try:
                remaining = self.proc.stdout.read()
                if remaining:
                    self._append(remaining)
            except Exception:
                pass
            self._append("\n--- Editor process exited ---")
            self.close_btn.config(text="Close")

    def _append(self, text: str) -> None:
        self.text.config(state=tk.NORMAL)
        self.text.insert(tk.END, text + "\n")
        self.text.see(tk.END)
        self.text.config(state=tk.DISABLED)

    def _on_close(self) -> None:
        self.destroy()


class EditorLauncher(tk.Tk):
    """Main launcher window."""

    def __init__(self):
        super().__init__()
        self.title("vkEngine Editor Launcher")
        self.resizable(False, False)
        self.minsize(480, 320)

        # Container for the stacked frames
        self.container = tk.Frame(self)
        self.container.pack(fill=tk.BOTH, expand=True)

        self.main_menu = MainMenu(self.container, self)
        self.create_form = CreateProjectForm(self.container, self)

        self._show_main_menu()

    def _show_main_menu(self) -> None:
        self.create_form.pack_forget()
        self.main_menu.pack(fill=tk.BOTH, expand=True)
        self.geometry("480x320")

    def _show_create_form(self) -> None:
        self.main_menu.pack_forget()
        self.create_form.pack(fill=tk.BOTH, expand=True)
        self.geometry("540x430")


    def open_project(self) -> None:
        """Show a file dialog and launch the editor with the chosen config."""
        config = filedialog.askopenfilename(
            title="Select editorconfig.json",
            filetypes=[("Editor config", "editorconfig.json"), ("JSON files", "*.json")],
            initialdir=get_repo_root(),
        )
        if not config:
            return  

        proc = launch_editor(config)
        if proc:
            ConsoleWindow(self, proc,
                          title=f"Editor Output — {os.path.basename(os.path.dirname(config))}")

    def create_project(self) -> None:
        """Switch to the project creation form."""
        self._show_create_form()

class MainMenu(tk.Frame):
    """Two-button landing page."""

    def __init__(self, parent: tk.Frame, app: EditorLauncher):
        super().__init__(parent)
        self.app = app

        self.spacer_top = tk.Frame(self)
        self.spacer_top.pack(fill=tk.BOTH, expand=True)

        title = tk.Label(self, text="vkEngine Editor",
                         font=("Segoe UI", 18, "bold"))
        title.pack(pady=(0, 10))

        subtitle = tk.Label(self, text="Select an action to get started",
                            font=("Segoe UI", 10))
        subtitle.pack(pady=(0, 30))

        btn_frame = tk.Frame(self)
        btn_frame.pack()

        btn_open = tk.Button(btn_frame, text="Open Existing Project",
                             font=("Segoe UI", 11),
                             width=28, height=2,
                             command=self.app.open_project)
        btn_open.pack(pady=6)

        or_label = tk.Label(btn_frame, text="— or —",
                            font=("Segoe UI", 10))
        or_label.pack(pady=6)

        btn_create = tk.Button(btn_frame, text="Create New Project",
                               font=("Segoe UI", 11),
                               width=28, height=2,
                               command=self.app.create_project)
        btn_create.pack(pady=6)

        self.spacer_bottom = tk.Frame(self)
        self.spacer_bottom.pack(fill=tk.BOTH, expand=True)

        hint = tk.Label(self, text=f"Editor: {find_editor_exe() or 'not built'}",
                        font=("Segoe UI", 8), fg="gray")
        hint.pack(side=tk.BOTTOM, pady=4)


class CreateProjectForm(tk.Frame):
    """Form for configuring a new editor project."""

    def __init__(self, parent: tk.Frame, app: EditorLauncher):
        super().__init__(parent)
        self.app = app

        title = tk.Label(self, text="Create New Project",
                         font=("Segoe UI", 14, "bold"))
        title.pack(pady=(16, 20))

        form = tk.Frame(self)
        form.pack(padx=40, fill=tk.X)

        row = 0
        tk.Label(form, text="Project Name:", anchor="w").grid(
            row=row, column=0, sticky="w", pady=4)
        self.name_var = tk.StringVar()
        self.name_entry = tk.Entry(form, textvariable=self.name_var, width=40)
        self.name_entry.grid(row=row, column=1, pady=4, padx=(8, 0))
        row += 1

        tk.Label(form, text="Parent Directory:", anchor="w").grid(
            row=row, column=0, sticky="w", pady=4)
        dir_frame = tk.Frame(form)
        dir_frame.grid(row=row, column=1, pady=4, padx=(8, 0), sticky="ew")
        self.dir_var = tk.StringVar(value=get_repo_root())
        self.dir_entry = tk.Entry(dir_frame, textvariable=self.dir_var, width=32)
        self.dir_entry.pack(side=tk.LEFT)
        tk.Button(dir_frame, text="Browse...",
                  command=self._browse_dir).pack(side=tk.LEFT, padx=(4, 0))
        row += 1

        tk.Label(form, text="Window Width:", anchor="w").grid(
            row=row, column=0, sticky="w", pady=4)
        self.width_var = tk.StringVar(value="1920")
        tk.Entry(form, textvariable=self.width_var, width=12).grid(
            row=row, column=1, sticky="w", pady=4, padx=(8, 0))
        row += 1

        tk.Label(form, text="Window Height:", anchor="w").grid(
            row=row, column=0, sticky="w", pady=4)
        self.height_var = tk.StringVar(value="1080")
        tk.Entry(form, textvariable=self.height_var, width=12).grid(
            row=row, column=1, sticky="w", pady=4, padx=(8, 0))
        row += 1

        self.no_launch_var = tk.BooleanVar(value=False)
        tk.Checkbutton(form, text="Create only, do not launch editor",
                       variable=self.no_launch_var,
                       font=("Segoe UI", 9)).grid(
            row=row, column=0, columnspan=2, sticky="w", pady=(8, 0))
        row += 1

        btn_frame = tk.Frame(self)
        btn_frame.pack(pady=(20, 12))

        self.create_btn = tk.Button(btn_frame, text="Create and Launch",
                                    font=("Segoe UI", 11),
                                    width=18,
                                    command=self._create)
        self.create_btn.pack(side=tk.LEFT, padx=6)

        tk.Button(btn_frame, text="Cancel",
                  font=("Segoe UI", 11),
                  width=12,
                  command=self._cancel).pack(side=tk.LEFT, padx=6)

    def _browse_dir(self) -> None:
        chosen = filedialog.askdirectory(
            title="Select Parent Directory",
            initialdir=self.dir_var.get() or get_repo_root(),
        )
        if chosen:
            self.dir_var.set(chosen)

    def _validate(self) -> tuple[bool, str]:
        """Return (ok, error_message)."""
        name = self.name_var.get().strip()
        if not name:
            return False, "Project name cannot be empty."

        parent = self.dir_var.get().strip()
        if not parent:
            return False, "Parent directory cannot be empty."
        if not os.path.isdir(parent):
            return False, f"Parent directory does not exist:\n{parent}"

        target = os.path.join(parent, name)
        if os.path.exists(target):
            return False, f"Target directory already exists:\n{target}\n\nPlease choose a different name or parent directory."

        try:
            w = int(self.width_var.get().strip())
            h = int(self.height_var.get().strip())
            if w < 320 or h < 240:
                return False, "Minimum window size is 320×240."
        except ValueError:
            return False, "Window width and height must be valid integers."

        return True, ""

    def _create(self) -> None:
        ok, err = self._validate()
        if not ok:
            messagebox.showerror("Validation Error", err)
            return

        name = self.name_var.get().strip()
        parent = self.dir_var.get().strip()
        width = int(self.width_var.get().strip())
        height = int(self.height_var.get().strip())
        project_dir = os.path.join(parent, name)

        try:
            os.makedirs(project_dir, exist_ok=False)
            create_project_files(project_dir, width, height)
        except FileExistsError:
            messagebox.showerror("Error",
                                 f"Directory already exists:\n{project_dir}")
            return
        except OSError as e:
            messagebox.showerror("Error",
                                 f"Failed to create project directory:\n{e}")
            return

        config_path = os.path.join(project_dir, "editorconfig.json")

        if self.no_launch_var.get():
            messagebox.showinfo("Project Created",
                                f"Project created at:\n{project_dir}\n\n"
                                f"Config: {config_path}")
            return

        proc = launch_editor(config_path)
        if proc:
            ConsoleWindow(self.app, proc,
                          title=f"Editor Output — {name}")

    def _cancel(self) -> None:
        self.app._show_main_menu()

def main():
    app = EditorLauncher()
    app.mainloop()


if __name__ == "__main__":
    main()
