"""core.py - shared protocol helpers (hex encode, ACK envelope decode, Ds reply decode)."""

ACK_NAMES = {0: 'ok', 1: 'clamped', 2: 'rejected', 3: 'blocked', 4: 'locked', 5: 'nowater', 6: 'unsupported'}


def hexb(n, width):
    try:
        n = int(n)
    except Exception:
        n = 0
    if n < 0:
        n = 0
    return format(n, '0%dX' % width)


def find_ack(replies, seq):
    """Look for the "#SSR" ack matching seq among a list of UDP reply strings."""
    want = hexb(seq, 2)
    for r in replies:
        if len(r) >= 4 and r[0] == '#' and r[1:3].upper() == want:
            try:
                code = int(r[3], 16)
            except Exception:
                continue
            return {'code': code, 'name': ACK_NAMES.get(code, 'unknown(%d)' % code)}
    return None


DIF_MODE_NAMES = ['OFF', 'CONT', '10 SEC', '2H AFTER SLEEP', '4H AFTER SLEEP']


def decode_ds_reply(text):
    """Decode the diffuser's Ds/Dc status reply: Ds+MM+SS+TTTT+UUUU+VVVV+RR+LLLL (24 chars)."""
    if not text or text[:2] != 'Ds' or len(text) < 24:
        return None
    mm = int(text[2:4], 16)
    ss = int(text[4:6], 16)
    tttt = int(text[6:10], 16)
    uuuu = int(text[10:14], 16)
    vvvv = int(text[14:18], 16)
    rr = int(text[18:20], 16)
    llll = int(text[20:24], 16)
    mode_name = 'OUT OF WATER' if mm == 5 else (DIF_MODE_NAMES[mm] if mm < len(DIF_MODE_NAMES) else 'M%d' % mm)
    return ('mode=M%d(%s) strip=%d parfum=%dmin usage=%dmin avg=%dmin refills=%d/10 total=%d'
            % (mm, mode_name, ss, tttt, uuuu, vvvv, rr, llll))
