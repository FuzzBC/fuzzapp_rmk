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


# SmartTV device->app push groups (see updStatus() in the .ino) - each is a
# self-contained one-line status the board pushes on its own, asynchronously,
# not just as a direct reply to the command that happened to trigger them.
MOTION_STATUS_NAMES = ['auto-off', 'off', 'idle (armed)', 'triggered, front', 'triggered, bed']
DIF_SUMMARY_NAMES = {0: 'off', 1: 'on', 2: 'out of water', 3: 'no response', 4: 'parfum running'}
AMBIENT_SUMMARY_NAMES = {0: 'off', 1: 'on', 2: 'ready'}
TESTMODE_NAMES = ['none', 'TV on (forced)', 'TV off (forced)', 'UDPRAW stream (sim)',
                   'motion, front (sim)', 'motion, bed (sim)', 'diffuser test', 'lux test']

# Term-log envelope (see APP_LOG_CHAR/APP_LOG_HDR in _FuZzAPP_SmartTV_R4_DEF.h):
# '*' + level(1 hex) + source(2 hex) + seq(2 hex) + text, no delimiters. This is
# what most 'K' debug targets actually reply with over UDP (not Serial-only,
# despite that command's older description - only the eeprom_backup target
# (K16) is genuinely Serial-only, since it uses PRNT::_print() directly).
LOG_LEVEL_NAMES = {0: 'ERROR', 1: 'WARN', 2: 'INFO', 3: 'DEBUG', 4: 'SECTION', 5: 'GAP'}
LOG_SOURCE_NAMES = {
    0x00: 'SYS', 0x01: 'NET', 0x02: 'RTC', 0x03: 'EE', 0x04: 'APP', 0x05: 'LED',
    0x06: 'TV', 0x07: 'MOTION', 0x08: 'LUX', 0x09: 'DIF', 0x0A: 'UDPRAW',
    0x0B: 'AMBIENT', 0x0C: 'BME', 0x0D: 'TASK', 0x0E: 'HB', 0x0F: 'TEST',
}


def describe_reply(text):
    """Human-readable one-line summary of one raw SmartTV/diffuser reply
    string. Returns None if the text doesn't match any known packet shape -
    callers should fall back to showing the raw text in that case."""
    if not text:
        return None

    if text[0] == '*' and len(text) >= 6:
        try:
            level = int(text[1], 16)
            source = int(text[2:4], 16)
            seq = int(text[4:6], 16)
            body = text[6:].strip()
            level_name = LOG_LEVEL_NAMES.get(level, 'L%d' % level)
            source_name = LOG_SOURCE_NAMES.get(source, 'src %d' % source)
            if level == 4:  # SEC - section header, app draws a divider with this title
                return '[%s] --- %s ---' % (source_name, body or 'section')
            if level == 5:  # GAP - blank spacer, no real content
                return '[%s] (spacer)' % source_name
            return '[%s][%s] %s' % (level_name, source_name, body) if body else '[%s][%s] (empty)' % (level_name, source_name)
        except Exception:
            return None

    if text[0] == '#' and len(text) >= 4:
        try:
            code = int(text[3], 16)
            return 'Command acknowledged: %s' % ACK_NAMES.get(code, 'unknown (%d)' % code)
        except Exception:
            return 'Acknowledgement packet'

    try:
        if text[:1] == 's' and len(text) >= 11:
            tv, mo, ur, am, ds = (int(text[1:3], 16), int(text[3:5], 16), int(text[5:7], 16),
                                   int(text[7:9], 16), int(text[9:11], 16))
            mo_name = MOTION_STATUS_NAMES[mo] if mo < len(MOTION_STATUS_NAMES) else 'unknown (%d)' % mo
            return ('Status - TV %s, motion %s, ambilight stream %s, ambient mode %s, diffuser %s'
                    % ('on' if tv else 'off', mo_name, 'on' if ur else 'off',
                       AMBIENT_SUMMARY_NAMES.get(am, 'unknown (%d)' % am), DIF_SUMMARY_NAMES.get(ds, 'unknown (%d)' % ds)))
        if text[:1] == 'H' and len(text) >= 5:
            return 'Climate - %d C, %d%% humidity' % (int(text[1:3], 16), int(text[3:5], 16))
        if text[:1] == 'E' and len(text) >= 2:
            return 'LEDs %s' % ('enabled' if int(text[1], 16) else 'disabled')
        if text[:1] == 'M' and len(text) >= 5:
            return 'Ambient light level - %d' % int(text[1:5], 16)
        if text[:1] == 'w' and len(text) >= 5:
            return 'WiFi signal - %d/4 bars' % int(text[1:3], 16)
        if text[:1] == 'f' and len(text) >= 5:
            mask = int(text[1:5], 16)
            return 'Fault flags - %s' % ('0x%04X' % mask if mask else 'none')
        if text[:1] == 'p' and len(text) >= 5:
            return 'Diffuser parfum timer - %d min left' % int(text[1:5], 16)
        if text[:2] == 'Dh' and len(text) >= 4:
            count = int(text[2:4], 16)
            minutes = []
            pos = 4
            while pos + 4 <= len(text):
                minutes.append(int(text[pos:pos + 4], 16))
                pos += 4
            valid = minutes[:count]
            if not valid:
                return 'Refill history - no cycles recorded yet'
            return 'Refill history - %d cycle%s: %s min' % (
                count, '' if count == 1 else 's', ', '.join(str(m) for m in valid))
        if text[:2] == 'LM' and len(text) >= 3:
            return 'Max brightness supported - %d' % int(text[2:], 16)
        if text[:1] == '@' and len(text) >= 3:
            tm = int(text[1:3], 16)
            return 'Active test mode - %s' % (TESTMODE_NAMES[tm] if tm < len(TESTMODE_NAMES) else 'unknown (%d)' % tm)
        if text == 'k':
            return 'Keep-alive reply'
        if text[:1] == 'u' and len(text) >= 15:
            accum, avg, cnt, tot = (int(text[1:5], 16), int(text[5:9], 16), int(text[9:11], 16), int(text[11:15], 16))
            return 'Diffuser usage - %d min this cycle, %d min average, %d/10 refills (%d total)' % (accum, avg, cnt, tot)
        if text[:1] == 'e' and len(text) >= 3:
            result = int(text[1:3], 16)
            return 'EEPROM write - %s' % ('saved' if result == 0 else 'failed (code %d)' % result)
        if text[:1] == 'S' and len(text) >= 201:
            return 'Settings dump - all 50 settings received'
        if text[:2] == 'LC' or (len(text) == 6 and all(c in '0123456789ABCDEFabcdef' for c in text)):
            hexval = text[2:] if text[:2] == 'LC' else text
            if len(hexval) >= 6:
                return 'LED colour - #%s' % hexval[:6].upper()
        if text[:2] == 'LD':
            return 'LED dual colour reply'
    except Exception:
        return None

    ds_summary = decode_ds_reply(text)
    if ds_summary:
        return 'Diffuser status - %s' % ds_summary

    return None
