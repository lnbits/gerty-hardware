# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow>=11,<12", "cryptography>=44,<46"]
# ///
"""Local HTTPS image server. Run with uv run tools/test_server.py --help."""
import argparse
import datetime as dt
import hashlib
import ipaddress
import json
from pathlib import Path
import ssl
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "test-server"


def setup(host):
    DATA.mkdir(exist_ok=True)
    if (DATA / "key.pem").exists() or (DATA / "cert.pem").exists():
        raise SystemExit("Certificate or key already exists; move it aside before setup.")
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, host)])
    try:
        san = x509.IPAddress(ipaddress.ip_address(host))
    except ValueError:
        san = x509.DNSName(host)
    now = dt.datetime.now(dt.timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - dt.timedelta(days=1))
            .not_valid_after(now + dt.timedelta(days=365))
            .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
            .add_extension(x509.SubjectAlternativeName([san]), critical=False)
            .sign(key, hashes.SHA256()))
    key_path = DATA / "key.pem"
    key_path.touch(mode=0o600)
    key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM,
                         serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    pem = cert.public_bytes(serialization.Encoding.PEM).decode()
    (DATA / "cert.pem").write_text(pem)
    if not (DATA / "image.png").exists():
        sample(DATA / "image.png")
    print("Created server certificate, private key, and sample image.")


def sample(path):
    image = Image.new("RGB", (960, 540), "white")
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default(size=38)
    draw.text((40, 30), "GERTY / E-PAPER TEST", fill="black", font=font)
    draw.text((40, 90), "960 x 540 - HTTPS - PNG", fill="black",
              font=ImageFont.load_default(size=24))
    for x in range(880):
        gray = round(x * 255 / 879)
        draw.line((40 + x, 160, 40 + x, 280), fill=(gray,) * 3)
    for i in range(16):
        draw.rectangle((40 + i * 55, 300, 94 + i * 55, 355), fill=(i * 17,) * 3)
    for i in range(3):
        x = 40 + i * 310
        draw.rounded_rectangle((x, 400, x + 240, 485), radius=12, outline="black", width=3)
        draw.text((x + 20, 423), ["FETCH", "DECODE", "SLEEP"][i], fill="black",
                  font=ImageFont.load_default(size=28))
        if i < 2:
            draw.line((x + 245, 442, x + 300, 442), fill="black", width=3)
    image.save(path)


def make_handler(base_url, image_path, refresh):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            try:
                # Content-addressed URL prevents a changing source file from
                # returning different bytes under a manifest's old revision.
                if self.path == "/manifest.json":
                    data = image_path.read_bytes()
                    revision = hashlib.sha256(data).hexdigest()
                    snapshot = DATA / f"{revision}.png"
                    if not snapshot.exists():
                        snapshot.write_bytes(data)
                    body = json.dumps({"schema_version": 1,
                                       "image_url": f"{base_url}/images/{revision}.png",
                                       "image_revision": revision,
                                       "refresh_seconds": refresh}).encode()
                    content_type = "application/json"
                elif self.path.startswith("/images/"):
                    name = self.path.removeprefix("/images/")
                    if len(name) != 68 or not name.endswith(".png") or any(
                            c not in "0123456789abcdef" for c in name[:-4]):
                        self.send_error(404)
                        return
                    body = (DATA / name).read_bytes()
                    content_type = "image/png"
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(body)
            except FileNotFoundError:
                self.send_error(404, "Image not found")
    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="Computer's LAN IP or DNS name used by ESP32")
    parser.add_argument("--port", type=int, default=8443)
    parser.add_argument("--refresh", type=int, default=30)
    parser.add_argument("--image", type=Path, default=DATA / "image.png")
    parser.add_argument("--setup", action="store_true", help="Generate certificate and sample, then exit")
    args = parser.parse_args()
    if args.setup:
        setup(args.host)
        return
    if not 30 <= args.refresh <= 300:
        parser.error("--refresh must be between 30 and 300 seconds")
    with Image.open(args.image) as image:
        if image.format != "PNG" or image.size != (960, 540) or image.info.get("interlace"):
            parser.error("Image must be a non-interlaced 960x540 PNG")
    base_url = f"https://{args.host}:{args.port}"
    server = ThreadingHTTPServer(("0.0.0.0", args.port),
                                 make_handler(base_url, args.image, args.refresh))
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(DATA / "cert.pem", DATA / "key.pem")
    server.socket = context.wrap_socket(server.socket, server_side=True)
    print(f"Serving {base_url}/manifest.json", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
