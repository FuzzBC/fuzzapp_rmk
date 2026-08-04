"""compiler.py - Arduino compilation and flashing operations"""

import glob
import re
import subprocess
import threading
import os
import time
import sys
from queue import Queue, Empty

# Windows-specific flags to hide console windows.
#
# CREATE_NO_WINDOW alone hides arduino-cli.exe's own window, but arduino-cli
# spawns its own subprocess to actually do the upload (avrdude for AVR boards,
# bossac for the UNO R4, espota.py for ESP8266 OTA) and that tool doesn't know
# to hide itself. When the parent process has NO console at all (which is what
# CREATE_NO_WINDOW gives it), Windows auto-allocates a brand-new *visible*
# console for any console-subsystem child that doesn't request otherwise -
# that's the flash-of-a-console-window on every Compile/Flash. Giving
# arduino-cli its own console (CREATE_NEW_CONSOLE) but keeping that console
# hidden (SW_HIDE) fixes it: children then attach to the existing hidden
# console instead of allocating a new visible one.
if sys.platform == 'win32':
    CREATE_FLAGS = subprocess.CREATE_NEW_CONSOLE
    STARTUPINFO = subprocess.STARTUPINFO
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = subprocess.SW_HIDE
else:
    CREATE_FLAGS = 0
    si = None


class ArduinoCompiler:
    def __init__(self, arduino_cli_path="arduino-cli"):
        self.arduino_cli_path = arduino_cli_path
        self.current_process = None
        self.running = False
        self.output_queue = Queue()
        self.installed_cores = set()
        self._cores_checked = False
        self.start_time = None
        self.timeout_seconds = 300      # 5 minutes, per operation
        self.heartbeat_interval = 1.0   # seconds between progress ticks
        self.stall_notice_seconds = 8   # log a reassurance line after this much silence
        self.current_operation = None   # Track current operation for stop button logic

    def _check_installed_cores(self):
        """Check which cores are installed"""
        try:
            result = subprocess.run(
                [self.arduino_cli_path, "core", "list"],
                capture_output=True,
                text=True, encoding='utf-8', errors='replace',
                timeout=10,
                creationflags=CREATE_FLAGS,
                startupinfo=si
            )
            if result.returncode == 0:
                for line in result.stdout.split('\n'):
                    if line.strip():
                        parts = line.split()
                        if len(parts) >= 2:
                            core_name = f"{parts[0]}:{parts[1]}"
                            self.installed_cores.add(core_name)
        except Exception as e:
            print(f"Error checking installed cores: {e}")
            
    def stop(self):
        """Stop the current running operation"""
        if self.running and self.current_process:
            self.running = False
            try:
                self.current_process.terminate()
                self.current_process.wait(timeout=2)
            except:
                try:
                    self.current_process.kill()
                except:
                    pass
            return True
        return False

    def _ensure_core_installed(self, fqbn, callback=None):
        """Ensure the required core is installed"""
        core = ':'.join(fqbn.split(':')[:2])

        if not self._cores_checked:
            self._check_installed_cores()
            self._cores_checked = True

        if callback:
            callback("output", f"Checking for core: {core}")

        if core in self.installed_cores:
            if callback:
                callback("output", f"Core {core} already installed")
            return True, "Core already installed"

        if callback:
            callback("output", f"Installing core: {core}...")

        try:
            self.running = True
            self.current_operation = "core_install"
            self.start_time = time.time()
            process = subprocess.Popen(
                [self.arduino_cli_path, "core", "install", core],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True, encoding='utf-8', errors='replace',
                bufsize=1,
                creationflags=CREATE_FLAGS,
                startupinfo=si
            )

            return_code, timed_out = self._stream_process(process, callback, "Core install", quiet=False)
            self.running = False
            self.current_operation = None

            if timed_out:
                return False, "Core install timed out"

            if return_code == 0:
                self.installed_cores.add(core)
                if callback:
                    callback("output", f"Core {core} installed successfully")
                return True, "Core installed"
            else:
                if callback:
                    callback("error", "Failed to install core")
                return False, "Core installation failed"
        except Exception as e:
            self.running = False
            if callback:
                callback("error", f"Error installing core: {str(e)}")
            return False, f"Error: {str(e)}"

    def compile(self, sketch_path, fqbn="arduino:avr:uno", callback=None):
        if not os.path.exists(sketch_path):
            error = f"Sketch not found: {sketch_path}"
            if callback:
                callback("error", error)
            return False, error

        core_installed, core_msg = self._ensure_core_installed(fqbn, callback)
        if not core_installed:
            return False, core_msg

 # Use --verbose to get progress info, but filter output. Increase timeout for compilation/linking
        original_timeout = self.timeout_seconds
        self.timeout_seconds = 180  # 3 minutes for compilation
        self.current_operation = "compile"
        try:
            # --export-binaries puts the built .bin at a predictable
            # <sketchDir>/build/<fqbn-with-dots>/<sketchName>.ino.bin path (see
            # _compute_binary_path) instead of arduino-cli's internal temp
            # build-cache directory, which flash_ota needs to find the file to
            # upload directly via espota.py.
            cmd = [self.arduino_cli_path, "compile", "--fqbn", fqbn, "--export-binaries", "--verbose", sketch_path]
            return self._run_command(cmd, "Compilation", callback, quiet=True)
        finally:
            self.timeout_seconds = original_timeout
            self.current_operation = None

    def flash(self, sketch_path, port, fqbn="arduino:avr:uno", callback=None):
        # Use --verbose for flash to get progress information, but filter output
        self.current_operation = "flash"
        cmd = [self.arduino_cli_path, "upload", "-p", port, "--fqbn", fqbn, "--verbose", sketch_path]
        return self._run_command(cmd, "Flash", callback, quiet=True)

    # Uploading via "arduino-cli upload --port <ip>" routes through arduino-
    # cli's own mDNS-based board/port discovery, redone from scratch on every
    # single invocation. Empirically, on this network, that live discovery
    # only succeeds roughly 1 out of every 2-3 attempts - even back-to-back,
    # even with a longer --discovery-timeout. It also only gives a "." per
    # 1460-byte chunk sent (no real percentage) since arduino-cli never passes
    # espota.py's own -r/--progress flag, and there's no supported way to make
    # it do so (--upload-property overrides of the recipe string were tested
    # and silently ignored).
    #
    # Both problems disappear by calling espota.py ourselves, directly, the
    # same way arduino-cli's platform.txt recipe would - see
    # tools.esptool.upload.network_pattern in
    # esp8266/hardware/esp8266/<ver>/platform.txt. No discovery needed (we
    # already have the IP), and -r gives real "\rUploading: [====] NN%" lines.
    _OTA_MAX_ATTEMPTS = 3
    _OTA_RETRY_DELAY_S = 1.5

    def _find_esp8266_espota_tools(self):
        """Locate python3.exe and espota.py for the installed ESP8266 core.
        Resolved dynamically (glob under the Arduino data dir) rather than
        hardcoded, since the exact core/tool version varies by machine and
        changes on every ESP8266 core update."""
        data_dir = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Arduino15')
        espota_matches = sorted(glob.glob(os.path.join(
            data_dir, 'packages', 'esp8266', 'hardware', 'esp8266', '*', 'tools', 'espota.py')))
        python3_matches = sorted(glob.glob(os.path.join(
            data_dir, 'packages', 'esp8266', 'tools', 'python3', '*', 'python3.exe')))
        if not espota_matches or not python3_matches:
            return None, None
        return python3_matches[-1], espota_matches[-1]  # newest version if more than one installed

    @staticmethod
    def _compute_binary_path(sketch_path, fqbn):
        """Matches the layout arduino-cli's --export-binaries produces:
        <sketchDir>/build/<fqbn-with-dots>/<sketchBaseName>.ino.bin"""
        sketch_dir = os.path.dirname(os.path.abspath(sketch_path))
        sketch_name = os.path.splitext(os.path.basename(sketch_path))[0]
        fqbn_dir = fqbn.replace(':', '.')
        return os.path.join(sketch_dir, 'build', fqbn_dir, f'{sketch_name}.ino.bin')

    def flash_ota(self, sketch_path, ip_address, port=8266, fqbn="esp8266:esp8266:nodemcuv2", callback=None, ota_password=None):
        """Flash an Arduino sketch via WiFi OTA by invoking espota.py directly
        (bypasses arduino-cli's flaky network-port discovery entirely) with
        real percentage progress. Requires compile() to have run first with
        --export-binaries so the .bin exists at the predictable path."""
        bin_path = self._compute_binary_path(sketch_path, fqbn)
        if not os.path.exists(bin_path):
            error = f"Compiled binary not found: {bin_path} (compile first)"
            if callback:
                callback("error", error)
            return False, error

        python3_path, espota_path = self._find_esp8266_espota_tools()
        if not python3_path:
            error = "Could not locate espota.py / python3 for the ESP8266 core"
            if callback:
                callback("error", error)
            return False, error

        for attempt in range(1, self._OTA_MAX_ATTEMPTS + 1):
            ok, msg, transient = self._flash_ota_attempt(
                python3_path, espota_path, ip_address, port, ota_password, bin_path, callback, attempt)
            if ok or not transient or attempt == self._OTA_MAX_ATTEMPTS:
                return ok, msg
            if callback:
                callback("output", f"No response from the device this pass "
                                    f"(attempt {attempt}/{self._OTA_MAX_ATTEMPTS}) - retrying...")
            time.sleep(self._OTA_RETRY_DELAY_S)
        return False, "WiFi OTA failed"

    def _flash_ota_attempt(self, python3_path, espota_path, ip_address, port, ota_password, bin_path, callback, attempt):
        """One direct espota.py upload attempt. Returns (ok, message, was_transient_network_hiccup)."""
        try:
            self.running = True
            self.current_operation = "flash_ota"
            self.start_time = time.time()
            if callback:
                suffix = f" (attempt {attempt}/{self._OTA_MAX_ATTEMPTS})" if attempt > 1 else ""
                callback("start", f"WiFi OTA to {ip_address}:{port}...{suffix}")

            cmd = [python3_path, "-I", espota_path, "-i", ip_address, "-p", str(port),
                   "-a", ota_password or "", "-r", "-f", bin_path]

            self.current_process = subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', bufsize=1,
                creationflags=CREATE_FLAGS,
                startupinfo=si
            )

            captured_lines = []
            def _capturing_callback(status, message):
                if status == "output":
                    captured_lines.append(message)
                    # espota.py -r prints "\rUploading: [====    ] 45% " -
                    # each \r-flushed update arrives as its own line here
                    # (universal newlines treats lone \r as a line break too).
                    # Route the number to the progress bar, not the log - the
                    # log doesn't need dozens of these lines scrolling past.
                    match = re.search(r'(\d+)%', message)
                    if match and callback:
                        callback("progress_only", message)
                        return
                if callback:
                    callback(status, message)

            return_code, timed_out = self._stream_process(self.current_process, _capturing_callback, "WiFi OTA", quiet=False)
            self.running = False
            self.current_operation = None

            if timed_out:
                return False, "OTA timed out", False

            combined_output = "\n".join(captured_lines).lower()
            transient = any(m in combined_output for m in ("no answer", "no response from device"))
            failure_markers = ("fail", "error uploading", "bad answer")
            ota_failure = next((m for m in failure_markers if m in combined_output), None)

            if return_code == 0 and not ota_failure:
                if callback:
                    callback("success", "WiFi OTA completed successfully")
                return True, "WiFi OTA successful", False
            else:
                if transient:
                    return False, "No response from device", True
                if callback:
                    reason = f" ({ota_failure})" if ota_failure else f" with code {return_code}"
                    callback("error", f"WiFi OTA failed{reason}")
                return False, "WiFi OTA failed", False

        except Exception as e:
            self.running = False
            self.current_operation = None
            error = f"Error: {str(e)}"
            if callback:
                callback("error", error)
            return False, error, False

    def _stream_process(self, process, callback, operation_name, quiet=False):
        """Read a subprocess's combined stdout on a background thread and
        report it through callback, emitting a heartbeat "progress" tick
        every heartbeat_interval seconds even when the tool prints nothing -
        so the UI never looks frozen during silent stretches (core install,
        linking, OTA transfer, ...).

        Params:
            process:        started subprocess.Popen with stdout=PIPE, text mode
            callback:       fn(status, message) -> None; status one of
                             "output" | "progress" | "error"
            operation_name: label used in progress/timeout messages
            quiet:          if True, filter output to show only errors and key info

        Returns: (return_code, timed_out)
        """
        line_queue = Queue()

        def reader():
            # Any exception here (e.g. a decode error) must still reach the
            # sentinel below - otherwise the main loop never learns the process
            # finished and just spins on heartbeat ticks until the operation
            # timeout fires, reporting a false "timed out" for a build that
            # actually succeeded seconds in.
            try:
                for line in iter(process.stdout.readline, ''):
                    line_queue.put(line)
            except Exception as e:
                line_queue.put(f"[reader error: {e}]")
            line_queue.put(None)  # sentinel: stdout pipe closed

        threading.Thread(target=reader, daemon=True).start()

        last_output = time.time()
        last_stall_notice = 0
        while self.running:
            elapsed = time.time() - self.start_time
            remaining = max(0, self.timeout_seconds - int(elapsed))

            if remaining <= 0:
                process.terminate()
                if callback:
                    callback("error", f"{operation_name} timed out after {self.timeout_seconds}s")
                return None, True

            try:
                line = line_queue.get(timeout=self.heartbeat_interval)
            except Empty:
                # Nothing printed since the last tick - still alive, just quiet.
                silence = time.time() - last_output
                if callback:
                    # Only send progress updates, no "still working" messages
                    callback("progress", f"{operation_name}: {int(elapsed)}s elapsed, {remaining}s left")
                continue

            if line is None:
                break  # process finished producing output

            last_output = time.time()
            last_stall_notice = 0
            text = line.strip()
            self.output_queue.put(text)
            # arduino-cli's --verbose echoes the exact subprocess command it's
            # about to run (gcc/linker during compile, espota.py during OTA) -
            # a single quoted-path-plus-flags wall of text that's rarely useful
            # and looks alarming/like a hang. Drop it regardless of quiet mode
            # (still recorded in output_queue above for anyone who needs it).
            is_command_echo = text.startswith('"') and any(
                marker in text for marker in ('.exe"', 'python3"', '.py"'))
            if text and callback and not is_command_echo:
                # In quiet mode, only show errors and memory usage summary
                if quiet:
                    # Show lines containing error/warning/failed as whole words -
                    # a plain substring match also fired on paths like
                    # ".../tools/warnings/none-cflags", leaking the full raw
                    # compiler/linker command line (very noisy, looks "stuck").
                    if re.search(r'\b(error|warning|failed)\b', text, re.IGNORECASE):
                        callback("output", text)
                    # Show only the one-line memory usage summaries (they all
                    # start with ". "), not the box-drawing breakdown table
                    # underneath each one (e.g. "..IROM  306924  code in
                    # flash") - that detail row also contains "code in flash"
                    # as a substring, so a plain keyword match let it leak too.
                    if text.startswith('.') and any(
                        keyword in text.lower() for keyword in
                        ['sketch uses', 'global variables', 'instruction ram', 'code in flash', 'variables and constants']
                    ):
                        callback("output", text)
                    # Extract progress percentages without showing verbose output
                    if '%' in text:
                        # Still pass through for progress bar extraction, but mark as progress-only
                        callback("progress_only", text)
                else:
                    callback("output", text)

        if not self.running:
            process.terminate()
            return None, False

        process.wait()
        return process.returncode, False

    def _run_command(self, cmd, operation_name, callback=None, quiet=False):
        try:
            self.running = True
            self.start_time = time.time()
            if callback:
                callback("start", f"{operation_name} started...")
                if not quiet:
                    callback("output", f"Running: {' '.join(cmd)}")

            self.current_process = subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', bufsize=1,
                creationflags=CREATE_FLAGS,
                startupinfo=si
            )

            return_code, timed_out = self._stream_process(self.current_process, callback, operation_name, quiet)
            self.running = False
            self.current_operation = None

            if timed_out:
                return False, "Operation timed out"

            if return_code == 0:
                if callback:
                    callback("success", f"{operation_name} completed successfully")
                return True, f"{operation_name} successful"
            else:
                if callback:
                    callback("error", f"{operation_name} failed with code {return_code}")
                return False, f"{operation_name} failed"

        except FileNotFoundError:
            self.running = False
            error = "arduino-cli not found. Please install and add to PATH."
            if callback:
                callback("error", error)
            return False, error
        except Exception as e:
            self.running = False
            error = f"Error: {str(e)}"
            if callback:
                callback("error", error)
            return False, error

    def check_arduino_cli(self):
        """Check if arduino-cli is available"""
        try:
            result = subprocess.run(
                [self.arduino_cli_path, "version"],
                capture_output=True,
                text=True, encoding='utf-8', errors='replace',
                timeout=5,
                creationflags=CREATE_FLAGS,
                startupinfo=si
            )
            return result.returncode == 0, result.stdout
        except Exception as e:
            return False, str(e)
