#!/usr/bin/env python3
"""Create a local DPAPI-protected OTA key and sign RoboDesk firmware images."""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import secrets
import sys
from ctypes import wintypes
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.exceptions import InvalidSignature


ROOT = Path(__file__).resolve().parents[1]
PRIVATE_KEY_PATH = ROOT / ".ota-secrets" / "firmware-signing-key.dpapi"
PUBLIC_HEADER_PATH = ROOT / "FirmwareOtaKey.h"
SIGNING_CONTEXT = "RoboDeskSonicCharacter|{board}|{version}|{size}|{sha256}"


def signing_message(board: str, version: int, size: int, sha256: str) -> bytes:
    """Build the board-bound message signed by both firmware and release tooling."""
    return SIGNING_CONTEXT.format(
        board=board, version=version, size=size, sha256=sha256
    ).encode("ascii")


class DataBlob(ctypes.Structure):
    _fields_ = [("cbData", wintypes.DWORD), ("pbData", ctypes.POINTER(ctypes.c_ubyte))]


def _blob(data: bytes) -> tuple[DataBlob, object]:
    storage = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
    return DataBlob(len(data), ctypes.cast(storage, ctypes.POINTER(ctypes.c_ubyte))), storage


def _dpapi(data: bytes, *, protect: bool) -> bytes:
    if sys.platform != "win32":
        raise RuntimeError("The signing key is protected for this Windows user account.")

    crypt32 = ctypes.WinDLL("crypt32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    crypt32.CryptProtectData.argtypes = [
        ctypes.POINTER(DataBlob), wintypes.LPCWSTR, ctypes.POINTER(DataBlob),
        wintypes.LPVOID, wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(DataBlob)
    ]
    crypt32.CryptProtectData.restype = wintypes.BOOL
    crypt32.CryptUnprotectData.argtypes = [
        ctypes.POINTER(DataBlob), ctypes.POINTER(wintypes.LPWSTR),
        ctypes.POINTER(DataBlob), wintypes.LPVOID, wintypes.LPVOID,
        wintypes.DWORD, ctypes.POINTER(DataBlob)
    ]
    crypt32.CryptUnprotectData.restype = wintypes.BOOL
    kernel32.LocalFree.argtypes = [wintypes.LPVOID]
    kernel32.LocalFree.restype = wintypes.LPVOID
    source, source_storage = _blob(data)
    destination = DataBlob()
    flags = 0x1  # CRYPTPROTECT_UI_FORBIDDEN
    if protect:
        ok = crypt32.CryptProtectData(
            ctypes.byref(source), "RoboDesk OTA signing key", None, None, None,
            flags, ctypes.byref(destination)
        )
    else:
        ok = crypt32.CryptUnprotectData(
            ctypes.byref(source), None, None, None, None, flags, ctypes.byref(destination)
        )
    if not ok:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        return ctypes.string_at(destination.pbData, destination.cbData)
    finally:
        kernel32.LocalFree(destination.pbData)
        del source_storage


def public_header(public_pem: bytes) -> bytes:
    pem = public_pem.decode("ascii").strip()
    return (
        "#pragma once\n\n"
        "// Baseline for a fresh NVS after the initial v2 USB migration; OTA releases stage their own version.\n"
        "#define ROBODESK_OTA_INITIAL_VERSION 2UL\n\n"
        "// Public verification key. The matching private key is DPAPI-protected locally.\n"
        "static const char ROBODESK_OTA_PUBLIC_KEY_PEM[] = R\"ROBODESKOTA(\n"
        f"{pem}\n"
        ")ROBODESKOTA\";\n"
    ).encode("ascii")


def init_key() -> None:
    if PRIVATE_KEY_PATH.exists():
        raise RuntimeError(f"Signing key already exists: {PRIVATE_KEY_PATH}")

    private_key = ec.generate_private_key(ec.SECP256R1())
    private_pem = private_key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    )
    public_pem = private_key.public_key().public_bytes(
        serialization.Encoding.PEM,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    PRIVATE_KEY_PATH.parent.mkdir(parents=True, exist_ok=True)
    PRIVATE_KEY_PATH.write_bytes(_dpapi(private_pem, protect=True))
    PUBLIC_HEADER_PATH.write_bytes(public_header(public_pem))
    print(f"Protected signing key created for this Windows account: {PRIVATE_KEY_PATH}")
    print(f"Firmware verification key written to: {PUBLIC_HEADER_PATH}")


def init_production_key(out_dir: Path) -> None:
    """PD-4.12: offline production key. Encrypted PKCS#8 + a separate random passphrase file; not tied to this PC."""
    key_path = out_dir / "production-signing-key.pem"
    if key_path.exists():
        raise RuntimeError(f"Production key already exists: {key_path}")
    out_dir.mkdir(parents=True, exist_ok=True)
    passphrase = secrets.token_urlsafe(32)
    private_key = ec.generate_private_key(ec.SECP256R1())
    key_path.write_bytes(private_key.private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
        serialization.BestAvailableEncryption(passphrase.encode("ascii"))))
    (out_dir / "production-passphrase.txt").write_text(passphrase + "\n", encoding="ascii")
    public_pem = private_key.public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo)
    (out_dir / "production-public-key.pem").write_bytes(public_pem)
    (out_dir / "FirmwareOtaKey.production.h").write_bytes(public_header(public_pem))
    fingerprint = hashlib.sha256(public_pem).hexdigest()
    (out_dir / "README.txt").write_text(
        "RoboDesk OTA production signing key (PD-4.12).\n"
        f"Public key SHA-256: {fingerprint}\n"
        "Move production-signing-key.pem and production-passphrase.txt to TWO different offline media,\n"
        "verify both copies, then delete this folder from the PC. Never commit or sync them.\n"
        "Procedure and rotation plan: docs/companion/OTA_PRODUCTION_KEY.md in the robot repository.\n",
        encoding="ascii")
    print(f"Production key written to {out_dir} (passphrase in a separate file, not printed)")
    print(f"Public key SHA-256: {fingerprint}")


def load_signing_key(key_file: Path | None, passphrase_file: Path | None):
    if key_file is None:
        return serialization.load_pem_private_key(_dpapi(PRIVATE_KEY_PATH.read_bytes(), protect=False), password=None)
    if passphrase_file is None:
        raise RuntimeError("--key-file needs --passphrase-file.")
    passphrase = passphrase_file.read_text(encoding="ascii").strip().encode("ascii")
    return serialization.load_pem_private_key(key_file.read_bytes(), password=passphrase)


def sign_image(image_path: Path, version: int, board: str, key_file: Path | None = None, passphrase_file: Path | None = None) -> None:
    private_key = load_signing_key(key_file, passphrase_file)
    if not isinstance(private_key, ec.EllipticCurvePrivateKey):
        raise RuntimeError("Stored signing key is not an EC private key.")

    image = image_path.read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    message = signing_message(board, version, len(image), digest)
    signature = private_key.sign(message, ec.ECDSA(hashes.SHA256()))
    signature_path = Path(str(image_path) + ".sig")
    signature_path.write_text(f"{version}:{signature.hex()}\n", encoding="ascii")
    print(f"Signed {image_path.name}: {len(image)} bytes, SHA-256 {digest}")
    print(f"Signature: {signature_path}")


def verify_image(image_path: Path, signature_path: Path | None = None, board: str = "ESP32-S3", public_header_path: Path | None = None) -> None:
    image = image_path.read_bytes()
    signature_file = signature_path or Path(str(image_path) + ".sig")
    version_text, signature_hex = signature_file.read_text(encoding="ascii").strip().split(":", 1)
    version = int(version_text)
    if version < 1 or version > 0x7FFFFFFF:
        raise RuntimeError("Version must be between 1 and 2147483647.")
    signature = bytes.fromhex(signature_hex)
    header = (public_header_path or PUBLIC_HEADER_PATH).read_text(encoding="ascii")
    public_pem = header.split('R"ROBODESKOTA(\n', 1)[1].split("\n)ROBODESKOTA", 1)[0].encode("ascii")
    public_key = serialization.load_pem_public_key(public_pem)
    digest = hashlib.sha256(image).hexdigest()
    message = signing_message(board, version, len(image), digest)
    try:
        public_key.verify(signature, message, ec.ECDSA(hashes.SHA256()))
    except InvalidSignature as exc:
        raise RuntimeError("Signature does not match this firmware image.") from exc
    print(f"Signature valid: {image_path.name}, {len(image)} bytes, SHA-256 {digest}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("init-key", help="Generate the local protected signing key and firmware public key")
    prod_parser = sub.add_parser("init-production-key", help="Generate the offline production key (encrypted, separate passphrase file)")
    prod_parser.add_argument("--out", type=Path, required=True, help="Folder outside the repository and outside cloud sync")
    sign_parser = sub.add_parser("sign", help="Sign an application .bin for a RoboDesk target board")
    sign_parser.add_argument("image", type=Path)
    sign_parser.add_argument("--version", type=int, required=True, help="Monotonic release number (always increase)")
    sign_parser.add_argument("--board", choices=("ESP32-S3", "ESP32-C3", "ESP32-S3/robot-link-v1", "ESP32-C3/gateway-link-v1"), default="ESP32-S3")
    sign_parser.add_argument("--key-file", type=Path, help="Offline production key (default: local DPAPI key)")
    sign_parser.add_argument("--passphrase-file", type=Path)
    verify_parser = sub.add_parser("verify", help="Verify an application .bin and its .sig file")
    verify_parser.add_argument("image", type=Path)
    verify_parser.add_argument("signature", type=Path, nargs="?")
    verify_parser.add_argument("--board", choices=("ESP32-S3", "ESP32-C3", "ESP32-S3/robot-link-v1", "ESP32-C3/gateway-link-v1"), default="ESP32-S3")
    verify_parser.add_argument("--public-header", type=Path, help="FirmwareOtaKey header to verify against (default: FirmwareOtaKey.h)")
    args = parser.parse_args()
    try:
        if args.command == "init-key":
            init_key()
        elif args.command == "init-production-key":
            init_production_key(args.out)
        elif args.command == "sign":
            if not args.image.is_file() or args.image.suffix.lower() != ".bin":
                raise RuntimeError("Input must be an existing application .bin file.")
            if args.key_file is None and not PRIVATE_KEY_PATH.is_file():
                raise RuntimeError("No signing key yet; run `python tools/ota_signing.py init-key` first.")
            if args.version < 1 or args.version > 0x7FFFFFFF:
                raise RuntimeError("Version must be between 1 and 2147483647.")
            sign_image(args.image, args.version, args.board, args.key_file, args.passphrase_file)
        else:
            verify_image(args.image, args.signature, args.board, args.public_header)
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"OTA signing error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
