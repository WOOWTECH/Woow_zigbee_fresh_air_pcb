"""WO30109 NFC 設定協定的 Python 參考實作（App 端）。規格：docs/nfc-protocol.md。

只用標準函式庫（hashlib／hmac／struct），刻意不共用 C 程式碼：韌體的 fa_nfc.c 與這支各自實作同一份規格，
再用 vectors() 產生的固定測試向量互相對照（firmware/test/test_core.c 的 nfc_cross_language_vectors）。

  python3 tools/nfc_ref.py            # 自我測試＋印出測試向量
"""
import hashlib, hmac, struct

ADDR_STATE, ADDR_REQUEST, AREA_LEN = 0x040, 0x0E0, 160
HDR_LEN, PAYLOAD_LEN, STATE_EXTRA, TAG_LEN, NAME_LEN, REMOTES_MAX = 12, 84, 8, 16, 24, 8
T_STATE, T_REQUEST, T_ACK = 1, 2, 3
OK, ERR_AUTH, ERR_STALE, ERR_INVALID, ERR_FORMAT, ERR_CMD = range(6)
MB_GET_STATUS, MB_IDENTIFY, MB_LEARN_REMOTE, MB_FACTORY_RESET = 0x01, 0x02, 0x10, 0x7F
DI_MODES = {0: "全關", 1: "按一次", 2: "持續"}
DO_MODES = {0: "自鎖", 1: "點動", 2: "互鎖"}


def key_from_pin(pin: str) -> bytes:
    assert len(pin) == 8 and pin.isdigit(), "PIN 是 8 位數字"
    return hashlib.sha256(b"WO30109-NFC-v1:" + pin.encode()).digest()


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def header(t: int, gen: int, length: int) -> bytes:
    return b"WO" + bytes([t, 1]) + struct.pack("<IHH", gen, length, 0)


def payload(cfg: dict) -> bytes:
    p = bytes(cfg["di"]) + bytes(cfg["do"]) + struct.pack("<4I", *cfg["jog_ms"])
    name = cfg["name"].encode()[:NAME_LEN - 1]
    p += name + b"\0" * (NAME_LEN - len(name))
    remotes = cfg.get("remotes", [])[:REMOTES_MAX]
    p += bytes([len(remotes), 0, 0, 0]) + struct.pack("<8I", *(remotes + [0] * (REMOTES_MAX - len(remotes))))
    assert len(p) == PAYLOAD_LEN
    return p


def parse_payload(p: bytes) -> dict:
    jog = list(struct.unpack_from("<4I", p, 8))
    n = p[48]
    return {"di": list(p[0:4]), "do": list(p[4:8]), "jog_ms": jog,
            "name": p[24:24 + NAME_LEN].split(b"\0")[0].decode(errors="replace"),
            "remotes": list(struct.unpack_from("<8I", p, 52))[:n]}


def encode_request(cfg: dict, based_on_gen: int, key: bytes) -> bytes:
    """App 要寫到 0x0E0 的 112 bytes（手機端以 4-byte block 寫，不足補 0 到 block 邊界）"""
    body = header(T_REQUEST, based_on_gen, PAYLOAD_LEN) + payload(cfg)
    return body + hmac.new(key, body, hashlib.sha256).digest()[:TAG_LEN]


def encode_state(cfg: dict, gen: int, fw_version: int, hw_rev: bytes) -> bytes:
    body = header(T_STATE, gen, PAYLOAD_LEN + STATE_EXTRA) + payload(cfg) + struct.pack("<I", fw_version) + hw_rev
    return body + struct.pack("<H", crc16(body))


def decode_state(b: bytes) -> dict:
    """App 讀 0x040 起 106 bytes 後解析；CRC 錯回傳 None（可能讀到 ESP32 正在寫的半筆，再讀一次）"""
    if b[:4] != b"WO" + bytes([T_STATE, 1]) or crc16(b[:104]) != struct.unpack_from("<H", b, 104)[0]:
        return None
    gen, = struct.unpack_from("<I", b, 4)
    fw, = struct.unpack_from("<I", b, HDR_LEN + PAYLOAD_LEN)
    return {"gen": gen, "fw_version": fw, "hw_rev": b[100:104].rstrip(b"\0").decode(), **parse_payload(b[HDR_LEN:HDR_LEN + PAYLOAD_LEN])}


def decode_ack(b: bytes):
    """ESP32 處理完 REQUEST 後在 0x0E0 寫的 16 bytes：回傳 (所根據的 gen, 狀態碼)；還沒處理回傳 None"""
    if b[:4] != b"WO" + bytes([T_ACK, 1]):
        return None
    return struct.unpack_from("<I", b, 4)[0], b[HDR_LEN]


def mb_request(cmd: int, seq: int, data: bytes = b"", key: bytes = None, challenge: int = 0) -> bytes:
    body = bytes([ord("M"), cmd, seq, len(data)]) + data
    if cmd in (MB_LEARN_REMOTE, MB_FACTORY_RESET):
        body += hmac.new(key, body + struct.pack("<I", challenge), hashlib.sha256).digest()[:TAG_LEN]
    return body


def mb_parse_response(b: bytes) -> dict:
    assert b[0] == ord("R")
    r = {"cmd": b[1], "seq": b[2], "status": b[3], "payload": b[5:5 + b[4]]}
    if r["cmd"] == MB_GET_STATUS and r["status"] == OK:
        gen, relays, di, net, flags, fw, up, ch = struct.unpack("<IBBBBIII", r["payload"])
        r.update(gen=gen, relays=relays, di=di, net=net, learning=bool(flags & 1), fw_version=fw, uptime_s=up, challenge=ch)
    return r


# ---------------- 測試向量（firmware/test/test_core.c 用同樣的輸入，結果必須逐 byte 相同）----------------
VEC_PIN = "12345678"
VEC_CFG = {"di": [2, 1, 0, 2], "do": [2, 2, 1, 0], "jog_ms": [1000, 1000, 5000, 1000], "name": "客廳新風",
           "remotes": [0x3A5F2, 0x1B007]}


def vectors() -> dict:
    key = key_from_pin(VEC_PIN)
    return {"key": key.hex(),
            "request": encode_request(VEC_CFG, 7, key).hex(),
            "state_crc": "%04x" % crc16(encode_state(VEC_CFG, 8, 0x00030301, b"3.3\0")[:104]),
            "mb_learn": mb_request(MB_LEARN_REMOTE, 5, b"", key, 0xDEADBEEF).hex()}


if __name__ == "__main__":
    # 標準向量：SHA-256（FIPS 180-2 "abc"）、HMAC-SHA256（RFC 4231 case 2）、CRC-16/CCITT-FALSE（"123456789"＝29B1）
    assert hashlib.sha256(b"abc").hexdigest().startswith("ba7816bf")
    assert crc16(b"123456789") == 0x29B1
    key = key_from_pin(VEC_PIN)
    req = encode_request(VEC_CFG, 7, key)
    assert len(req) == 112
    st = encode_state(VEC_CFG, 8, 0x00030301, b"3.3\0")
    assert len(st) == 106 and decode_state(st)["name"] == "客廳新風" and decode_state(st)["remotes"] == [0x3A5F2, 0x1B007]
    bad = bytearray(st); bad[30] ^= 1
    assert decode_state(bytes(bad)) is None
    for k, v in vectors().items():
        print(f"{k:10s} {v}")
    print("nfc_ref 自我測試 OK")
