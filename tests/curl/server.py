# The curl test's HTTPS server (tests/run.sh): serves `hello' with the
# certificate and key given, on the port given, until killed
#
# usage: server.py <cert> <key> <port> <ready file>

import http.server, ssl, sys

cert, key, port, ready = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]

class Hello(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"aquila-tls-ok\n"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass

server = http.server.HTTPServer(("127.0.0.1", port), Hello)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(cert, key)
server.socket = ctx.wrap_socket(server.socket, server_side=True)

open(ready, "w").close()
server.serve_forever()
