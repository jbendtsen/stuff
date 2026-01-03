#!/usr/bin/env python3

import sys
import traceback
import http.server
import urllib.request
import serial

ID_HDR = "x-client-id"

class SerialServer(http.server.HTTPServer):
    def __init__(self, server_address, handler_class):
        super().__init__(server_address, handler_class)
        self.serial_connections = []

class SerialServerHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.handle_serial_request()
    def do_POST(self):
        self.handle_serial_request()

    def handle_serial_request(self):
        try:
            location = str(self.path)
            print(location)
            if location.startswith("/open"):
                self.open_serial_connection()
            elif location.startswith("/close"):
                self.close_serial_connection()
            elif location.startswith("/read"):
                self.read_serial()
            elif location.startswith("/write"):
                self.write_serial()
            elif location.startswith("/flush"):
                self.flush_serial()
            elif location.startswith("/drain"):
                self.drain_serial()
            else:
                raise KeyError("Unrecognized endpoint " + location)
        except Exception as e:
            msg = "{0} @ {1}: {2}".format(get_full_type_name(e), location, e)
            print(msg)
            traceback.print_exc(file=sys.stdout)
            self.send_response(400)
            self.end_headers()
            self.wfile.write(bytes(msg, "utf8"))

    def get_this_serial(self):
        client_id = int(self.headers.get(ID_HDR))
        s = self.server.serial_connections[client_id-1]
        if not s:
            raise LookupError("Could not find client id {0}".format(client_id))
        return s

    def open_serial_connection(self):
        attrs = dict()
        for k in self.headers:
            if k.startswith("x-"):
                value = self.headers.get(k)
                if k.endswith("port") or k.endswith("parity"):
                    attrs[k[2:]] = value
                elif k.endswith("timeout"):
                    attrs[k[2:]] = float(value)
                else:
                    attrs[k[2:]] = int(value)

        s = serial.Serial(**attrs)
        # s = DummySerial(**attrs)
        self.server.serial_connections.append(s)
        self.send_response(200)
        self.send_header(ID_HDR, str(len(self.server.serial_connections)))
        self.end_headers()

    def close_serial_connection(self):
        s = self.get_this_serial()
        s.close()
        self.server.serial_connections[int(self.headers.get(ID_HDR)) - 1] = None
        self.send_response(200)
        self.end_headers()

    def read_serial(self):
        size = int(self.headers.get("x-size"))
        s = self.get_this_serial()
        print(not s)
        data = s.read(size)
        self.send_response(200)
        self.send_header("content-length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def write_serial(self):
        s = self.get_this_serial()
        size = int(self.headers.get("content-length"))
        if size <= 0:
            return
        res = s.write(self.rfile.read(size))
        self.send_response(200)
        self.end_headers()
        self.wfile.write(bytes(str(res), "utf8"))

    def flush_serial(self):
        s = self.get_this_serial()
        s.flush()
        self.send_response(200)
        self.end_headers()

    def drain_serial(self):
        s = self.get_this_serial()
        data = s.read_all()
        self.send_response(200)
        self.send_header("content-length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

class SerialClient:
    def __init__(self, remote_addr, client_id=-1):
        self.remote_addr = remote_addr
        self.client_id = client_id
        self.is_open = False
        self.port = None
        self.baudrate = None
        self.parity = None
        self.timeout = None
        self.write_timeout = None

    # Note that 'port' here refers to the serial port on the server
    def start(self, port=None, baudrate=115200, parity=serial.PARITY_EVEN, timeout=6.0, write_timeout=6.0):
        self.port = port
        self.baudrate = baudrate
        self.parity = parity
        self.timeout = timeout
        self.write_timeout = write_timeout

        data = locals()
        hdrs = dict()
        for k in data:
            if k != "self" and k != "remote_addr" and data[k] is not None:
                hdrs["x-" + k] = str(data[k])

        self.is_open = False

        url = self.remote_addr + "/open"
        req = urllib.request.Request(url=url, headers=hdrs)

        try:
            with urllib.request.urlopen(req) as conn:
                self.client_id = conn.headers[ID_HDR]
            self.is_open = True
        except urllib.error.HTTPError as e:
            raise extract_server_error(e)

    def perform_request(self, action, **kwargs):
        hdrs = dict()
        data = None
        for k in kwargs:
            if k == "body":
                data = kwargs[k]
            else:
                hdrs["x-" + k] = kwargs[k]
        hdrs[ID_HDR] = self.client_id
        url = self.remote_addr + "/" + action

        req = urllib.request.Request(url=url, data=data, headers=hdrs)
        error = None
        try:
            with urllib.request.urlopen(req) as conn:
                return conn.read()
        except urllib.error.HTTPError as e:
            error = extract_server_error(e)
        if error:
            raise error

    def read(self, size: int) -> bytes:
        return self.perform_request("read", size=size)

    def write(self, data: bytes):
        return self.perform_request("write", body=data)

    def flush(self):
        return self.perform_request("flush")

    def read_all(self) -> bytes:
        return self.perform_request("drain")

    def close(self):
        self.is_open = False
        return self.perform_request("close")

def extract_server_error(e):
    try:
        contents = str(e.fp.read(), "utf8")
    except:
        return e
    try:
        pos = contents.index(" ")
        kind = contents[0:pos]
        msg = contents[pos+1:]
        print(pos, kind, msg)
        return globals()[kind](msg)
    except:
        try:
            return Exception(contents)
        except Exception as err:
            return err

def get_full_type_name(obj):
    t = type(obj)
    if t.__module__ == "builtins":
        return t.__qualname__
    return t.__module__ + "." + t.__qualname__

class DummySerial:
    def __init__(self, **kwargs):
        self.is_open = True
        self.fd = 0

    def read(self, size: int) -> bytes:
        return bytes()

    def write(self, data: bytes):
        return 0

    def flush(self):
        pass

    def read_all(self) -> bytes:
        return bytes()

    def close(self):
        self.is_open = False

def bail_on_error(e):
    msg = "{0}: {1}\n".format(get_full_type_name(e), str(e).replace(":", ":\n"))
    sys.stderr.write(msg)
    sys.stderr.flush()
    exit(1)

def print_help(args):
    msg = [
        "Pyserial Client/Server",
        "Usage: {0} <action> [args...]".format(args[0]),
        "Actions:",
        "  serve <IP port>",
        "  open  <remote addr> --port <serial port> --baudrate <bits per second> --parity <even/odd> --timeout <seconds>",
        "  close <remote addr> <client id>",
        "  read  <remote addr> <client id> <byte size>",
        "  write <remote addr> <client id> [filename, else stdin]",
        "  flush <remote addr> <client id>",
        "  drain <remote addr> <client id>",
        ""
    ]
    sys.stderr.write("\n".join(msg))
    sys.stderr.flush()

def main(args):
    if len(args) < 3 or args[1].endswith("help"):
        print_help(args)
        return

    if args[1] == "serve":
        port = int(args[2])
        server = SerialServer(("", port), SerialServerHandler)
        server.serve_forever()
        return

    remote_addr = args[2]
    if args[1] == "open":
        params = dict()
        i = 2
        while i < len(args) - 1:
            i += 1
            if args[i] == "--port":
                i += 1
                params["port"] = args[i]
            elif args[i] == "--baudrate":
                i += 1
                params["baudrate"] = int(args[i])
            elif args[i] == "--parity":
                i += 1
                params["parity"] = args[i][0].upper()
            elif args[i] == "--timeout":
                i += 1
                params["timeout"] = float(args[i])
                params["write_timeout"] = float(args[i])

        s = SerialClient(remote_addr)
        s.start(**params)
        print("Client ID:", s.client_id)
        return

    try:
        client_id = int(args[3])
    except:
        print_help(args)
        return

    if args[1] == "close":
        s = SerialClient(remote_addr, client_id)
        s.close()

    elif args[1] == "read":
        size = int(args[4])
        try:
            size = int(args[4])
        except:
            print_help(args)
            return
        s = SerialClient(remote_addr, client_id)
        data = s.read(size)
        sys.stdout.buffer.write(data)
        sys.stdout.flush()

    elif args[1] == "write":
        s = SerialClient(remote_addr, client_id)
        if len(args) > 4:
            fname = args[4]
            with open(fname, "rb") as f:
                s.write(f.read())
        else:
            s.write(sys.stdin.buffer.read())

    elif args[1] == "flush":
        s = SerialClient(remote_addr, client_id)
        s.flush()

    elif args[1] == "drain":
        s = SerialClient(remote_addr, client_id)
        data = s.read_all()
        sys.stdout.buffer.write(data)
        sys.stdout.flush()

if __name__ == "__main__":
    main(sys.argv)
