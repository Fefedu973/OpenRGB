"""Minimal Room SDK7 image client example; no third-party packages or bridge.

Lists capabilities by default. --gradient explicitly sends one synthetic image.
The connection is stop-and-wait for image ACKs; it never queues obsolete frames.
"""
import argparse
import socket
import struct
import time

REQUEST_OUTPUTS = 0x524D0001
UPDATE_IMAGE = 0x524D0002
MAGIC = 0x474D4952
IMAGE_FLAG = 1 << 29
MAX_BYTES = 64 * 1024 * 1024

class Client:
    def __init__(self, host='127.0.0.1', port=6742, version=7):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.version = version
        reply = self.request(40, payload=struct.pack('<I', version), reply_id=40)
        self.version = min(version, struct.unpack('<I', reply)[0])
        self.flags = 0
        if self.version >= 6:
            flags = self.request(52, payload=struct.pack('<I', 1), reply_id=53)
            self.flags = struct.unpack('<I', flags)[0]

    def close(self):
        self.sock.close()

    def exact(self, size):
        parts = bytearray()
        while len(parts) < size:
            block = self.sock.recv(size-len(parts))
            if not block:
                raise ConnectionError('SDK connection closed')
            parts.extend(block)
        return bytes(parts)

    def request(self, packet, device=0, payload=b'', reply_id=None):
        self.sock.sendall(struct.pack('<4sIII', b'ORGB', device, packet, len(payload))+payload)
        reply = None
        acknowledged = False
        deadline = time.monotonic()+5
        while True:
            self.sock.settimeout(max(0.001, deadline-time.monotonic()))
            magic, dev, kind, size = struct.unpack('<4sIII', self.exact(16))
            if magic != b'ORGB' or size > MAX_BYTES+100:
                raise ValueError('Invalid SDK envelope')
            body = self.exact(size)
            if kind == 10 and dev == device and len(body) == 8:
                acked, status = struct.unpack('<II', body)
                if acked == packet:
                    if status:
                        raise RuntimeError(f'SDK request {packet:#x}: status {status}')
                    acknowledged = True
            if kind == reply_id and dev == device:
                reply = body
                if packet == 40:
                    self.version = min(self.version, struct.unpack('<I', body)[0])
            if (reply_id is None or reply is not None) and (self.version < 6 or acknowledged):
                return reply
            if time.monotonic() >= deadline:
                raise TimeoutError('SDK response timed out')

    def controllers(self):
        reply = self.request(0, reply_id=0)
        count, = struct.unpack_from('<I', reply)
        if self.version < 6:
            return list(range(count))
        if len(reply) != 4+4*count:
            raise ValueError('Malformed controller list')
        return list(struct.unpack_from(f'<{count}I', reply, 4))

    def outputs(self, device):
        if self.version < 7 or not self.flags & IMAGE_FLAG:
            return []
        reply = self.request(REQUEST_OUTPUTS, device, reply_id=REQUEST_OUTPUTS)
        magic, schema, count = struct.unpack_from('<III', reply)
        if magic != MAGIC or schema != 1 or count > 4096 or len(reply) != 12+16*count:
            raise ValueError('Not Room image schema1')
        return [dict(zip(('zone', 'width', 'height', 'max_fps'), struct.unpack_from('<IIII', reply, 12+16*i))) for i in range(count)]

    def send_image(self, device, zone, width, height, bgra, sequence=1, lease_ms=1500,
                   mapping=(0., 0., 1., 0., 0., 1., 1.), stride=None):
        if self.version < 7 or not self.flags & IMAGE_FLAG:
            raise RuntimeError('Peer has no Room SDK7 image capability')
        stride = stride or width*4
        if not (0 < width <= 16384 and 0 < height <= 16384 and stride >= width*4
                and len(bgra) == stride*height <= MAX_BYTES and 100 <= lease_ms <= 5000):
            raise ValueError('Invalid image size or lease')
        header = struct.pack('<8IQ7dI', MAGIC, 1, zone, width, height, stride, 1, lease_ms,
                             sequence, *mapping, len(bgra))
        self.request(UPDATE_IMAGE, device, header+bgra)

def gradient(width, height):
    pixels = bytearray(width*height*4)
    for y in range(height):
        for x in range(width):
            i = (y*width+x)*4
            pixels[i:i+4] = bytes((64, round(255*y/max(1,height-1)), round(255*x/max(1,width-1)), 255))
    return bytes(pixels)

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', default='127.0.0.1')
    p.add_argument('--port', type=int, default=6742)
    p.add_argument('--gradient', nargs=2, type=int, metavar=('DEVICE_ID','ZONE'))
    a=p.parse_args()
    client=Client(a.host,a.port)
    try:
        for device in client.controllers():
            print({'device':device, 'image_outputs':client.outputs(device)})
        if a.gradient:
            client.send_image(*a.gradient,800,600,gradient(800,600))
            print('800x600 synthetic image accepted; lease 1500ms (not optical confirmation).')
    finally:
        client.close()
