"""Minimal independent KeyAuth 1.3 client; no downloaded SDK execution or auto-installs.
Protocol/signing key reference: KeyAuth-Python-Example, commit
5f3894bab5e133e902222c04b0a98df149a41667 (see THIRD_PARTY.md).
"""
from __future__ import annotations

import hashlib
import json
import os
import time
import uuid
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import Request, build_opener, HTTPRedirectHandler

APP_NAME = "Data-Tools"
OWNER_ID = "2bTkDP2v6c"
APP_VERSION = "1.0"
VERIFY_KEY = "5586b4bc69c7a4b487e4563a4cd96afd39140f919bd31cea7d1c6a1e8439422b"
API_URL = "https://keyauth.win/api/1.3/"


class LicenseError(Exception):
    pass


class NoRedirects(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        raise LicenseError("KeyAuth: перенаправление отклонено")


def getchecksum() -> str:
    """SDK-compatible MD5 of the launcher; not a tamper-proof client integrity mechanism."""
    launcher = Path(__file__).resolve().parents[1] / "Data-Tools.py"
    return hashlib.md5(launcher.read_bytes(), usedforsecurity=False).hexdigest()


def device_id() -> str:
    """Stable per-install ID; does NOT collect serial numbers or /etc/machine-id."""
    home = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "data-tools"
    home.mkdir(parents=True, exist_ok=True, mode=0o700)
    path = home / "device-id"
    if not path.exists():
        try:
            fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        except FileExistsError:
            pass
        else:
            with os.fdopen(fd, "w") as stream:
                stream.write(str(uuid.uuid4()))
    value = path.read_text().strip()
    try:
        return str(uuid.UUID(value))
    except ValueError:
        raise LicenseError("Некорректный локальный device-id") from None


def verify_response(body: bytes, signature: str, timestamp: str, *, now: float | None = None, key: str = VERIFY_KEY) -> dict:
    try:
        from nacl.signing import VerifyKey
        from nacl.exceptions import BadSignatureError
    except ImportError:
        raise LicenseError("Установите зависимости: python -m pip install -r requirements.txt") from None
    try:
        age = abs((time.time() if now is None else now) - int(timestamp))
        if age > 25:
            raise LicenseError("KeyAuth: время ответа не прошло проверку. Проверьте часы устройства")
        VerifyKey(bytes.fromhex(key)).verify(timestamp.encode("ascii") + body, bytes.fromhex(signature))
        result = json.loads(body)
        if not isinstance(result, dict) or not isinstance(result.get("success"), bool):
            raise ValueError("invalid payload")
        return result
    except LicenseError:
        raise
    except (ValueError, TypeError, UnicodeError, OverflowError, BadSignatureError):
        raise LicenseError("KeyAuth: некорректный или неподписанный ответ; доступ запрещён") from None


class KeyAuth:
    def __init__(self):
        self.session = ""
        self.authorized = False
        self.opener = build_opener(NoRedirects())

    def _request(self, values: dict) -> dict:
        data = {**values, "name": APP_NAME, "ownerid": OWNER_ID}
        request = Request(API_URL, data=urlencode(data).encode(), method="POST",
                          headers={"User-Agent": "Data-Tools/1.0", "Content-Type": "application/x-www-form-urlencoded"})
        try:
            with self.opener.open(request, timeout=15) as response:
                body = response.read(262145)
                if len(body) > 262144:
                    raise LicenseError("KeyAuth: слишком большой ответ")
                result = verify_response(body, response.headers.get("x-signature-ed25519", ""),
                                         response.headers.get("x-signature-timestamp", ""))
        except LicenseError:
            raise
        except (OSError, ValueError):
            # Never include request data, credentials, session or raw server text in exceptions.
            raise LicenseError("KeyAuth недоступен или HTTP-запрос отклонён. Поиск не запускался") from None
        if result.get("ownerid", OWNER_ID) != OWNER_ID:
            raise LicenseError("KeyAuth: ответ относится к другому приложению")
        if result["success"] is not True:
            raise LicenseError("KeyAuth отклонил запрос: проверьте лицензию, версию, hash и HWID в панели приложения")
        return result

    def authenticate(self, license_key: str) -> None:
        self.authorized = False
        self.session = ""
        if not license_key or len(license_key) > 256 or any(c.isspace() for c in license_key):
            raise LicenseError("Пустой или некорректный формат лицензии")
        response = self._request({"type": "init", "ver": APP_VERSION, "hash": getchecksum()})
        session = response.get("sessionid")
        if not isinstance(session, str) or not 1 <= len(session) <= 256:
            raise LicenseError("KeyAuth не вернул корректную сессию")
        self.session = session
        try:
            self._request({"type": "license", "key": license_key, "hwid": device_id(), "sessionid": self.session})
        except Exception:
            self.session = ""
            raise
        self.authorized = True
