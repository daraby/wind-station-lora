import os
import socket
import struct
from fastapi import FastAPI
from influxdb_client import InfluxDBClient, Point, WritePrecision
import uvicorn

INFLUX_URL = os.environ["INFLUX_URL"]
INFLUX_TOKEN = os.environ["INFLUX_TOKEN"]
INFLUX_ORG = os.environ["INFLUX_ORG"]
INFLUX_BUCKET = os.environ["INFLUX_BUCKET"]
UDP_PORT = int(os.environ.get("UDP_PORT", "1700"))

app = FastAPI()

client = InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
write_api = client.write_api()

def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc

last_seq_by_node = {}

def parse_frame(pkt: bytes):
    if len(pkt) < 1+1+4+4+2+2+2+2+1+2:
        return None
    body = pkt[:-2]
    got_crc = struct.unpack(">H", pkt[-2:])[0]
    calc_crc = crc16_ccitt(body)
    if got_crc != calc_crc:
        return None

    node_id = pkt[0]
    msg_type = pkt[1]
    seq = struct.unpack(">I", pkt[2:6])[0]
    ts = struct.unpack(">I", pkt[6:10])[0]
    avg_x10 = struct.unpack(">H", pkt[10:12])[0]
    gust_x10 = struct.unpack(">H", pkt[12:14])[0]
    dir_deg = struct.unpack(">H", pkt[14:16])[0]
    bat_mv = struct.unpack(">H", pkt[16:18])[0]
    flags = pkt[18]
    return node_id, msg_type, seq, ts, avg_x10, gust_x10, dir_deg, bat_mv, flags

def build_ack(node_id: int, last_seq: int) -> bytes:
    body = struct.pack(">BBI", node_id, 100, last_seq)
    crc = crc16_ccitt(body)
    return body + struct.pack(">H", crc)

def write_influx(node_id, seq, ts, avg_x10, gust_x10, dir_deg, bat_mv, msg_type):
    avg = avg_x10 / 10.0
    gust = gust_x10 / 10.0
    t_ns = int(ts) * 1_000_000_000

    p = (
        Point("wind_station")
        .tag("node_id", str(node_id))
        .field("wind_avg_1m", float(avg))
        .field("gust_1m", float(gust))
        .field("dir_avg_1m", int(dir_deg))
        .field("battery_mv", int(bat_mv))
        .field("seq", int(seq))
        .field("msg_type", int(msg_type))
        .time(t_ns, WritePrecision.NS)
    )
    write_api.write(bucket=INFLUX_BUCKET, record=p)

def udp_loop():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", UDP_PORT))
    print(f"UDP listening on :{UDP_PORT}")

    while True:
        pkt, addr = sock.recvfrom(512)
        parsed = parse_frame(pkt)
        if not parsed:
            continue

        node_id, msg_type, seq, ts, avg_x10, gust_x10, dir_deg, bat_mv, flags = parsed
        write_influx(node_id, seq, ts, avg_x10, gust_x10, dir_deg, bat_mv, msg_type)

        last_seq = last_seq_by_node.get(node_id, 0)
        if seq > last_seq:
            last_seq_by_node[node_id] = seq
            last_seq = seq

        ack = build_ack(node_id, last_seq)
        sock.sendto(ack, addr)

@app.on_event("startup")
def on_start():
    import threading
    t = threading.Thread(target=udp_loop, daemon=True)
    t.start()

@app.get("/health")
def health():
    return {"ok": True, "last_seq": last_seq_by_node}

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)
