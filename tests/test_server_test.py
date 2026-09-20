"""Run with the Pillow/cryptography versions documented in README.md."""
import importlib.util
import json
from pathlib import Path
import ssl
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import urlopen

spec = importlib.util.spec_from_file_location(
    "server", Path(__file__).resolve().parents[1] / "tools/test_server.py")
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)


class HttpsServerTest(unittest.TestCase):
    def test_seeed_diagnostic_size(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "seeed.png"
            server.sample(path, (800, 480))
            with server.Image.open(path) as image:
                self.assertEqual(image.size, (800, 480))
                self.assertEqual(image.format, "PNG")
                self.assertFalse(image.info.get("interlace"))

    def test_certificate_revision_snapshot_and_private_file_isolation(self):
        with tempfile.TemporaryDirectory() as temporary:
            server.ROOT = Path(temporary)
            server.DATA = server.ROOT / "test-server"
            (server.ROOT / "include").mkdir()
            server.setup("127.0.0.1")
            image = server.DATA / "image.png"
            httpd = server.ThreadingHTTPServer(("127.0.0.1", 0), server.BaseHTTPRequestHandler)
            base = f"https://127.0.0.1:{httpd.server_port}"
            httpd.RequestHandlerClass = server.make_handler(base, image, 30)
            tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            tls.load_cert_chain(server.DATA / "cert.pem", server.DATA / "key.pem")
            httpd.socket = tls.wrap_socket(httpd.socket, server_side=True)
            thread = threading.Thread(target=httpd.serve_forever, daemon=True)
            thread.start()
            context = ssl.create_default_context(cafile=str(server.DATA / "cert.pem"))

            def get(path):
                with urlopen(base + path, context=context, timeout=5) as response:
                    return response.read()

            try:
                original = image.read_bytes()
                first = json.loads(get("/manifest.json"))
                self.assertEqual(first["refresh_seconds"], 30)
                self.assertEqual(first, json.loads(get("/manifest.json")))
                self.assertEqual(get(first["image_url"][len(base):]), original)
                server.Image.new("RGB", (960, 540), "black").save(image)
                second = json.loads(get("/manifest.json"))
                self.assertNotEqual(first["image_revision"], second["image_revision"])
                self.assertEqual(get(first["image_url"][len(base):]), original)
                self.assertEqual(get(second["image_url"][len(base):]), image.read_bytes())
                for path in ("/key.pem", "/images/../../include/trust.h", "/missing"):
                    with self.assertRaises(HTTPError) as error:
                        get(path)
                    self.assertEqual(error.exception.code, 404)
                with urlopen(base + "/manifest.json",
                             context=ssl._create_unverified_context(), timeout=5) as response:
                    self.assertEqual(json.load(response), second)
            finally:
                httpd.shutdown()
                httpd.server_close()
                thread.join()
