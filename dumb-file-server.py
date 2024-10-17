#!/usr/bin/env python3

# Really dumb HTTP upload and download server
# No security, other than requiring specific request headers, which can be configured
# Client-side download: curl -O -H "key: value" ipaddress:port/filename
# Client-side upload: curl -X PUT -H "key: value" ipaddress:port/filename --data-binary @filename

import os
import sys
import http.server
import socketserver

class DumbFileServer(http.server.BaseHTTPRequestHandler):
    # Class variables. Yes, this is intentional.
    working_dir = "."
    port = 0
    required_headers = {}

    def verify_and_configure(self):
        self.server_version = "dumb file server"
        is_valid = True
        keys = DumbFileServer.required_headers.keys()
        for k in keys:
            if k not in self.headers or self.headers[k] != DumbFileServer.required_headers[k]:
                is_valid = False
                break
        if not is_valid:
            self.send_response(404)
            self.end_headers()
            return False
        return True

    def respond_with_body(self, body, code=200):
        self.send_response(code)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def respond_with_body_string(self, msg, code=200):
        self.respond_with_body(bytes(msg, "utf8"), code=code)

    def get_location(self):
        location = self.path
        if len(location) <= 1:
            self.respond_with_body_string("no location given\n", code=404)
            return ""
        while location[0] == "/":
            location = location[1:]
        return location

    def do_GET(self):
        if not self.verify_and_configure():
            return

        location = self.get_location()
        if not location:
            return

        sent_any_data = False
        filepath = os.path.join(DumbFileServer.working_dir, location)
        try:
            size = os.path.getsize(filepath)
            if size > 0:
                with open(filepath, "rb") as f:
                    chunksize = 256 * 1024
                    pos = 0
                    while pos < size:
                        chunk = f.read(min(size - pos, chunksize))
                        if not sent_any_data:
                            self.send_response(200)
                            self.send_header("Content-Length", str(size))
                            self.end_headers()
                        self.wfile.write(chunk)
                        sent_any_data = True
                        pos += chunksize
        except:
            pass

        if not sent_any_data:
            print("failed to open file \"" + filepath + "\"")
            self.respond_with_body_string("could not open file\n", code=404)

    def do_POST(self):
        self.do_PUT()

    def do_PUT(self):
        if not self.verify_and_configure():
            return

        location = self.get_location()
        if not location:
            return

        size = int(self.headers["content-length"])
        with open(os.path.join(DumbFileServer.working_dir, location), "wb") as f:
            f.write(self.rfile.read(size))

        self.respond_with_body_string("received " + str(size) + " bytes\n")

def print_help():
    print("dumb http file server")
    print("download with GET, upload with PUT")
    print("options:")
    print("  help")
    print("  -h")
    print("     prints this help menu")
    print("  dir <working directory>")
    print("  -d <working directory>")
    print("     path to folder where files are accessed")
    print("  port <port number>")
    print("  -p <port number>")
    print("     listen on a particular port")
    print("     defaults to 0, ie. automatic")
    print("  require <key>=<value>")
    print("  -r <key>=<value>")
    print("     require that every request contain the http header <key>=<value>")
    print("     this option can be used more than once, where every invocation adds a requirement")
    print("     *must be used at least once*")

def main(args):
    if len(args) == 1 or (len(args) > 1 and (args[1] == "help" or args[1] == "-h")):
        print_help()
        return

    working_dir = ""
    port = 0
    headers = {}

    idx = 1
    while idx < len(args):
        opt = args[idx]
        if opt == "-d" or opt == "dir":
            idx += 1
            working_dir = args[idx]
        elif opt == "-p" or opt == "port":
            idx += 1
            port = int(args[idx])
        elif opt == "-r" or opt == "require":
            idx += 1
            kv = args[idx].split("=")
            if len(kv) != 2:
                print("invalid requirement header: must contain a key and value, separated by a an '='")
                return
            headers[kv[0]] = kv[1]
        idx += 1

    if not headers:
        print("no requirement headers were given, use '-r' or 'require' at least once")
        return

    if not working_dir:
        working_dir = "."

    DumbFileServer.working_dir = working_dir
    DumbFileServer.port = port
    DumbFileServer.required_headers = headers

    with socketserver.TCPServer(("", port), DumbFileServer) as httpd:
        print("serving on", httpd.server_address)
        try:
            httpd.serve_forever()
        except BaseException as e:
            print(" " + e.__class__.__name__)
            if e.args:
                print(e.args)
            if str(e):
                print(e)
            print("Exiting")
            return

main(sys.argv)
