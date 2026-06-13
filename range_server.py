import os
import re
import socketserver
from http.server import SimpleHTTPRequestHandler

PORT = 8002
DIRECTORY = "." # Serve files from the current directory

BYTE_RANGE_RE = re.compile(r'bytes=(\d+)-(\d+)?$')

def parse_byte_range(byte_range_header):
    if byte_range_header.strip() == '':
        return None, None
    m = BYTE_RANGE_RE.match(byte_range_header)
    if not m:
        raise ValueError('Invalid byte range %s' % byte_range_header)
    groups = m.groups()
    first = int(groups[0])
    last = int(groups[1]) if groups[1] else None
    if last and last < first:
        raise ValueError('Invalid byte range %s' % byte_range_header)
    return first, last

class RangeHTTPRequestHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Accept-Ranges", "bytes")
        super().end_headers()

    def do_GET(self):
        f = self.send_head()
        if f:
            try:
                range_header = self.headers.get('Range')
                if range_header:
                    try:
                        start, end = parse_byte_range(range_header)
                        file_size = os.fstat(f.fileno()).st_size

                        if start is None:
                            start = 0
                        if end is None:
                            end = file_size - 1

                        if start >= file_size or end >= file_size:
                            self.send_error(416, "Requested Range Not Satisfiable")
                            return

                        self.send_response(206)
                        self.send_header("Content-Range", f"bytes {start}-{end}/{file_size}")
                        self.send_header("Content-Length", (end - start + 1))
                        self.end_headers()

                        f.seek(start)
                        self.copy_byte_range(f, self.wfile, start, end)
                    except ValueError as e:
                        super().do_GET()
                else:
                    self.copyfile(f, self.wfile)
            finally:
                f.close()

    def copy_byte_range(self, infile, outfile, start, stop, bufsize=16*1024):
        infile.seek(start)
        while True:
            to_read = min(bufsize, stop + 1 - infile.tell())
            if to_read <= 0:
                break
            buf = infile.read(to_read)
            if not buf:
                break
            outfile.write(buf)

if __name__ == "__main__":
    os.chdir(DIRECTORY)
    # Use Allow Reuse Address
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", PORT), RangeHTTPRequestHandler) as httpd:
        print(f"Serving HTTP on port {PORT} from directory '{DIRECTORY}'")
        httpd.serve_forever()
