"""network.py - Serial and Telnet communication for FlashConsole

Provides unified interface for both serial port communication and
telnet connections for console monitoring.
"""

import codecs
import serial
import serial.tools.list_ports
import socket
import subprocess
import sys
import threading
import time
from queue import Queue

# Hide the console window ping.exe would otherwise flash on screen every time
# it's launched from this windowless (.pyw) app - same reasoning as
# compiler.py's CREATE_FLAGS.
if sys.platform == 'win32':
    _PING_CREATE_FLAGS = subprocess.CREATE_NO_WINDOW
    _ping_si = subprocess.STARTUPINFO()
    _ping_si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    _ping_si.wShowWindow = subprocess.SW_HIDE
else:
    _PING_CREATE_FLAGS = 0
    _ping_si = None


class SerialConnection:
    """Serial port connection for device communication"""
    
    def __init__(self, port, baudrate=115200, timeout=1):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.connection = None
        self.running = False
        self.receive_thread = None
        self.data_queue = Queue()
        self.buffer = ""  # Buffer for incomplete lines
        # Incremental decoder keeps partial multi-byte UTF-8 sequences (e.g. a
        # box-drawing char split across two reads) buffered internally instead
        # of mangling each half into a "?" replacement character.
        self._decoder = codecs.getincrementaldecoder('utf-8')(errors='replace')

    def connect(self):
        """Open serial connection"""
        try:
            self.connection = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=self.timeout
            )
            self.running = True
            self.receive_thread = threading.Thread(target=self._receive_loop, daemon=True)
            self.receive_thread.start()
            return True, "Connected"
        except Exception as e:
            return False, str(e)
            
    def disconnect(self):
        """Close serial connection"""
        self.running = False
        if self.receive_thread:
            self.receive_thread.join(timeout=1)
        if self.connection:
            try:
                self.connection.close()
            except:
                pass
            self.connection = None
            
    def send(self, data):
        """Send data to serial port"""
        if self.connection and self.connection.is_open:
            try:
                self.connection.write(data.encode('utf-8'))
                return True, "Sent"
            except Exception as e:
                return False, str(e)
        return False, "Not connected"
        
    def _receive_loop(self):
        """Background thread to receive data"""
        while self.running and self.connection and self.connection.is_open:
            try:
                if self.connection.in_waiting > 0:
                    data = self.connection.read(self.connection.in_waiting)
                    text = self._decoder.decode(data)

                    # Clean up control characters and line endings
                    text = text.replace('\r\n', '\n').replace('\r', '\n')
                    # Remove problematic control characters but keep newlines and tabs
                    cleaned_text = ''.join(char for char in text if char.isprintable() or char in '\n\t')
                    
                    if cleaned_text:
                        # Add to buffer and process complete lines
                        self.buffer += cleaned_text
                        while '\n' in self.buffer:
                            line, self.buffer = self.buffer.split('\n', 1)
                            if line.strip():  # Only send non-empty lines
                                self.data_queue.put(line)
                time.sleep(0.01)
            except Exception as e:
                if self.running:
                    self.data_queue.put(f"Error: {str(e)}")
                break
                
    def get_data(self):
        """Get received data from queue"""
        data = []
        while not self.data_queue.empty():
            data.append(self.data_queue.get())
        return data
        
    def is_connected(self):
        """Check if connected"""
        return self.connection and self.connection.is_open


class TelnetConnection:
    """Telnet connection for console monitoring"""
    
    def __init__(self, host, port=23, timeout=5):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.connection = None
        self.running = False
        self.receive_thread = None
        self.data_queue = Queue()
        self.buffer = ""  # Buffer for incomplete lines
        self._decoder = codecs.getincrementaldecoder('utf-8')(errors='replace')

    def connect(self):
        """Open telnet connection"""
        try:
            self.connection = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.connection.settimeout(self.timeout)
            self.connection.connect((self.host, self.port))
            self.running = True
            self.receive_thread = threading.Thread(target=self._receive_loop, daemon=True)
            self.receive_thread.start()
            return True, "Connected"
        except Exception as e:
            # socket.socket() above already assigned self.connection before
            # the .connect() call that actually failed - leaving it set here
            # made is_connected() (which just checked "is not None") report
            # a failed/timed-out connection as connected.
            if self.connection:
                try:
                    self.connection.close()
                except Exception:
                    pass
                self.connection = None
            return False, str(e)
            
    def disconnect(self):
        """Close telnet connection"""
        self.running = False
        if self.receive_thread:
            self.receive_thread.join(timeout=1)
        if self.connection:
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except:
                pass
            try:
                self.connection.close()
            except:
                pass
            self.connection = None
            
    def send(self, data):
        """Send data to telnet server"""
        if self.connection:
            try:
                self.connection.sendall((data + "\r\n").encode('utf-8'))
                return True, "Sent"
            except Exception as e:
                return False, str(e)
        return False, "Not connected"
        
    def _receive_loop(self):
        """Background thread to receive data"""
        while self.running and self.connection:
            try:
                self.connection.settimeout(0.1)
                data = self.connection.recv(4096)
                if data:
                    text = self._decoder.decode(data)

                    # Normalize line endings
                    text = text.replace('\r\n', '\n').replace('\r', '\n')
                    
                    if text:
                        # Add to buffer and process complete lines
                        self.buffer += text
                        while '\n' in self.buffer:
                            line, self.buffer = self.buffer.split('\n', 1)
                            if line.strip():  # Only send non-empty lines
                                self.data_queue.put(line)
                else:
                    break
            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    self.data_queue.put(f"Error: {str(e)}")
                break
        # The loop only exits when the connection is gone (peer closed it -
        # e.g. our own firmware dropping this client because a new one just
        # connected - or a real socket error). Without this, is_connected()
        # kept reporting "connected" for a link the server had already torn
        # down, since self.running was otherwise only ever reset by an
        # explicit disconnect() call.
        self.running = False

    def get_data(self):
        """Get received data from queue"""
        data = []
        while not self.data_queue.empty():
            data.append(self.data_queue.get())
        return data
        
    def is_connected(self):
        """Check if connected"""
        # self.running is only set True after a successful connect() and
        # False on disconnect()/receive-loop exit - checking it too (not just
        # "is self.connection an object") is what actually distinguishes a
        # live connection from a failed one, now that connect() also cleans
        # up self.connection on failure.
        return self.running and self.connection is not None


def get_serial_ports():
    """Get list of available serial ports"""
    try:
        ports = serial.tools.list_ports.comports()
        return [(port.device, port.description) for port in ports]
    except Exception as e:
        print(f"Error getting serial ports: {e}")
        return []


def test_connection(host, port, timeout=2):
    """Test if a host:port is reachable"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        result = sock.connect_ex((host, port))
        sock.close()
        return result == 0
    except:
        return False


def ping_host(host, timeout_ms=1000):
    """ICMP ping via the OS 'ping' command (no raw sockets, so no admin
    rights needed on Windows). This checks basic network/WiFi reachability
    at the OS level - an ESP8266 answers ping as soon as it has an IP,
    before its own sketch has even finished setup(), so it reports "online"
    sooner than a TCP probe against an application-level port (telnet,
    OTA, ...) would, especially right after a reboot."""
    # 2 packets, not 1 - a single dropped ICMP echo (common enough on WiFi)
    # would otherwise read as "offline" when the device is actually fine.
    if sys.platform == 'win32':
        cmd = ['ping', '-n', '2', '-w', str(timeout_ms), host]
    else:
        cmd = ['ping', '-c', '2', '-W', str(max(1, timeout_ms // 1000)), host]
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, encoding='utf-8', errors='replace',
            timeout=(timeout_ms / 1000) * 2 + 2,
            creationflags=_PING_CREATE_FLAGS, startupinfo=_ping_si
        )
        # Windows' ping.exe can return 0 even for "Destination host
        # unreachable" replies (from an intermediate router, not the target),
        # so confirm an actual reply from the host rather than trusting the
        # exit code alone. Any one of the 2 packets getting a reply counts.
        return 'ttl=' in result.stdout.lower()
    except Exception:
        return False
