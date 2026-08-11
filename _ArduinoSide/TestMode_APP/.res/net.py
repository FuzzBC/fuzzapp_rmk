"""net.py - direct-socket UDP/TCP helpers for TestMode.pyw.

No subprocess spawning (unlike the HTA variant, which had to shell out to
PowerShell for every send since JScript has no socket access). Real sockets
in-process means sends are effectively instant and only genuinely block for
as long as a timeout actually requires.
"""

import socket
import time


def send_udp(ip, port, payload, timeout_ms=1200, max_timeout_ms=None):
    """Send one UDP datagram, then collect every reply that arrives before
    the wait window elapses (an enveloped "#SS<cmd>" can draw two packets:
    the command's own reply, then the "#SSR" ack).

    timeout_ms is an IDLE window, not a fixed total budget: every time a
    reply packet actually arrives, the window resets for another
    timeout_ms of waiting - so a slow multi-packet exchange (the diffuser
    relay commands routinely draw an ack, then a second hop's real reply
    from the diffuser itself, sometimes seconds apart) keeps getting more
    time as long as something keeps showing up, instead of being cut off
    by a deadline fixed before the first byte was even sent. A hard
    ceiling (max_timeout_ms, default 4x timeout_ms floored at 6000ms)
    still applies so a device that just keeps talking can't hang the
    caller forever - once that's hit, whatever arrived so far is returned.

    Both firmwares reply to a FIXED port (the same port they listen on),
    never to the actual source port of the incoming request (no
    remotePort() tracking - see udpSend()/beginPacket(g_udpSenderIP,
    UDP_PORT) in the diffuser .ino). The real phone app works because it
    binds its own local socket to that same fixed port instead of using a
    random ephemeral one; without binding here, every reply gets sent to a
    local port nothing is listening on and is silently dropped - which
    looks exactly like every command timing out, even though the device
    received and processed everything fine."""
    if max_timeout_ms is None:
        max_timeout_ms = max(timeout_ms * 4, 6000)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind(('', port))
    except Exception:
        pass  # fall back to an ephemeral port if the fixed one is already in use
    try:
        sock.sendto(payload.encode('utf-8'), (ip, port))
    except Exception as e:
        sock.close()
        return {'status': 'ERROR', 'message': 'send failed: %s' % e, 'replies': []}

    replies = []
    reply_bytes = []
    now = time.time()
    hard_deadline = now + max_timeout_ms / 1000.0
    idle_deadline = now + timeout_ms / 1000.0
    while True:
        remain = min(idle_deadline, hard_deadline) - time.time()
        if remain <= 0:
            break
        sock.settimeout(remain)
        try:
            data, _addr = sock.recvfrom(4096)
            reply_bytes.append(data)
            replies.append(data.decode('utf-8', errors='replace'))
            idle_deadline = time.time() + timeout_ms / 1000.0  # more may still be coming - extend
        except socket.timeout:
            break
        except Exception:
            break
    sock.close()

    if not replies:
        return {'status': 'TIMEOUT', 'message': 'no reply within %dms' % timeout_ms, 'replies': [], 'reply_bytes': []}
    return {'status': 'OK', 'replies': replies, 'reply_bytes': reply_bytes}


def send_tcp(ip, port, payload, timeout_ms=1500):
    """Open a TCP connection (telnet console port), write payload as one
    CRLF-terminated line, collect whatever text arrives within timeout_ms,
    then close. Console replies (S/D/?/banner) can span several lines, so
    this reads for the whole window rather than stopping at the first chunk."""
    try:
        sock = socket.create_connection((ip, port), timeout=timeout_ms / 1000.0)
    except socket.timeout:
        return {'status': 'TIMEOUT', 'message': 'connect timed out after %dms' % timeout_ms, 'replies': []}
    except Exception as e:
        return {'status': 'ERROR', 'message': 'connect failed: %s' % e, 'replies': []}

    try:
        sock.sendall((payload + '\r\n').encode('utf-8'))
        chunks = []
        deadline = time.time() + timeout_ms / 1000.0
        while True:
            remain = deadline - time.time()
            if remain <= 0:
                break
            sock.settimeout(remain)
            try:
                data = sock.recv(4096)
                if not data:
                    break
                chunks.append(data)
            except socket.timeout:
                break
    except Exception as e:
        sock.close()
        return {'status': 'ERROR', 'message': str(e), 'replies': []}

    sock.close()
    text = b''.join(chunks).decode('utf-8', errors='replace').strip()
    if not text:
        return {'status': 'TIMEOUT', 'message': 'no reply within %dms' % timeout_ms, 'replies': []}
    return {'status': 'OK', 'replies': [text]}
