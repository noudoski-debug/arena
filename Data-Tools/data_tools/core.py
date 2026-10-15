"""Evidence graph, privacy-conscious serialization, and bounded HTTP transport."""
from __future__ import annotations

import hashlib
import json
import os
import re
import tempfile
import unicodedata
from datetime import datetime, timezone
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit, urlunsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener

VERSION = "1.0.0"
PUBLIC_FIELDS = frozenset({"title", "name", "username", "organization", "description", "summary", "text", "url", "website", "tags", "source", "date", "category", "project", "id"})
EMAIL = re.compile(r"[\w.+-]+@[\w.-]+\.[A-Za-z]{2,}")
PHONE = re.compile(r"(?<!\w)\+?\d[\d ()-]{7,}\d(?!\w)")
SECRET = re.compile(r"(?i)\b(password|passwd|token|api[_-]?key|secret|authorization)\b\s*[:=]\s*[^\s,;]+")


def redact(value: object, limit: int = 1800) -> str:
    text = str(value)
    text = SECRET.sub(r"\1=[hidden]", text)
    text = EMAIL.sub("[email hidden]", text)
    text = PHONE.sub("[number hidden]", text)
    return text[:limit]


def normalized(value: object) -> str:
    return unicodedata.normalize("NFKC", str(value)).casefold()


def public_record(record: dict) -> dict[str, str]:
    """Unknown and sensitive columns are not searched or imported."""
    result = {}
    for key, value in record.items():
        field = str(key).strip().casefold()
        if field not in PUBLIC_FIELDS or value is None:
            continue
        if isinstance(value, (str, int, float, bool)):
            result[field] = redact(value)
        elif isinstance(value, list):
            result[field] = redact(", ".join(str(v) for v in value[:30] if isinstance(v, (str, int, float))))
    return result


def safe_url(value: str) -> str:
    try:
        parts = urlsplit(str(value))
        if parts.scheme not in ("http", "https") or not parts.hostname or parts.username or parts.password:
            return ""
        # Don't turn a sensitive identifier into a clickable URL. Query strings can contain keys.
        if redact(parts.path, 10000) != parts.path:
            return ""
        return urlunsplit((parts.scheme, parts.netloc, parts.path, "", ""))[:1800]
    except (ValueError, TypeError):
        return ""


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


class Graph:
    def __init__(self, query: str):
        self.query = redact(query, 240)
        self.nodes: dict[str, dict] = {}
        self.edges: dict[tuple[str, str, str], dict] = {}
        self.sources: list[dict] = []
        self.root = self.node("query", self.query, identity="root")

    def node(self, kind: str, label: str, *, identity: str = "", url: str = "", details: dict | None = None) -> str:
        label = redact(label, 160)
        identity = identity or safe_url(url) or label
        key = kind + ":" + hashlib.sha256(identity.encode()).hexdigest()[:20]
        cleaned = {redact(k, 80): redact(v) for k, v in (details or {}).items()}
        if key not in self.nodes:
            self.nodes[key] = {"id": key, "kind": kind, "label": label, "url": safe_url(url), "details": cleaned}
        else:
            self.nodes[key]["details"].update(cleaned)
        return key

    def edge(self, source: str, target: str, label: str) -> None:
        if source not in self.nodes or target not in self.nodes:
            raise ValueError("Edge endpoints must exist")
        key = (source, target, label)
        self.edges[key] = {"source": source, "target": target, "label": label}

    def source(self, name: str, state: str, count: int, note: str = "") -> None:
        self.sources.append({"name": redact(name, 160), "state": state, "count": count, "note": redact(note), "checked_at": utc_now()})

    def export(self) -> dict:
        return {"app": "Data-Tools", "version": VERSION, "query": self.query, "generated_at": utc_now(),
                "notice": "Связи означают наблюдаемые факты из указанных источников, а не доказанную личность. Нет доступа ко всем утечкам или закрытым Telegram-данным.",
                "nodes": list(self.nodes.values()), "edges": list(self.edges.values()), "sources": self.sources}


class SourceError(Exception):
    pass


class SameHostRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if urlsplit(newurl).scheme != "https" or urlsplit(newurl).hostname != urlsplit(req.full_url).hostname:
            raise SourceError("Источник перенаправил запрос на другой хост; переход отклонён")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


class HttpClient:
    HOSTS = frozenset({"api.github.com", "t.me", "www.bing.com", "api.search.brave.com"})

    def __init__(self, timeout: float = 15):
        self.timeout = timeout
        self.opener = build_opener(SameHostRedirect())

    def fetch(self, url: str, headers: dict | None = None) -> bytes:
        parts = urlsplit(url)
        if parts.scheme != "https" or parts.hostname not in self.HOSTS or parts.username or parts.password:
            raise SourceError("Разрешены только HTTPS-запросы к поддерживаемым источникам")
        request = Request(url, headers={"User-Agent": "Data-Tools/1.0 (authorized public-source research)", **(headers or {})})
        try:
            with self.opener.open(request, timeout=self.timeout) as response:
                body = response.read(2_000_001)
                if len(body) > 2_000_000:
                    raise SourceError("Ответ источника превышает лимит 2 МБ")
                return body
        except HTTPError as exc:
            messages = {401: "нужен действительный API-ключ", 403: "доступ запрещён или исчерпан лимит API",
                        404: "не найдено", 429: "лимит запросов; повторите позже"}
            raise SourceError(f"HTTP {exc.code}: {messages.get(exc.code, 'ошибка источника')}") from None
        except (URLError, TimeoutError, OSError) as exc:
            # Exception URLs may include API queries or credentials; don't echo them.
            raise SourceError("Сетевая ошибка или тайм-аут; проверьте доступ к источнику") from None

    def json(self, url: str, headers: dict | None = None):
        try:
            return json.loads(self.fetch(url, {"Accept": "application/json", **(headers or {})}))
        except (ValueError, UnicodeError, RecursionError):
            raise SourceError("Источник вернул некорректный JSON") from None


def atomic_write(path: Path, content: str, *, force: bool = False) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if not force:
        # Exclusive create also works on Android shared storage, which lacks hard links.
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(content)
        return
    fd, temporary = tempfile.mkstemp(prefix=".data-tools-", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            stream.write(content)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
