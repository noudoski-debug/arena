"""Public-source adapters. No private Telegram sessions, leak dumps or credential searches."""
from __future__ import annotations

import csv
import json
import os
import re
import xml.etree.ElementTree as ET
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import quote, urlencode

from .core import Graph, HttpClient, SourceError, normalized, public_record, redact, safe_url

TELEGRAM_NAME = re.compile(r"[A-Za-z][A-Za-z0-9_]{3,31}\Z")
SOCIAL_NAME = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.-]{0,38}\Z")


def username(value: str, *, telegram: bool = False) -> str:
    value = value.strip().removeprefix("@")
    if not (TELEGRAM_NAME if telegram else SOCIAL_NAME).fullmatch(value):
        raise SourceError("Некорректный username: передайте только ник, без URL, пробелов и invite-ссылок")
    return value


def search_web(graph: Graph, client: HttpClient, words: str, limit: int = 20, *, social: bool = False) -> None:
    provider = "Brave Search API" if os.environ.get("DATA_TOOLS_BRAVE_KEY") else "Bing RSS"
    query = words
    if social:
        query = f'"{words}" (site:vk.com OR site:instagram.com OR site:tiktok.com OR site:x.com OR site:youtube.com OR site:reddit.com)'
    if provider == "Brave Search API":
        data = client.json("https://api.search.brave.com/res/v1/web/search?" + urlencode({"q": query, "count": min(limit, 20)}),
                           {"X-Subscription-Token": os.environ["DATA_TOOLS_BRAVE_KEY"]})
        if not isinstance(data, dict) or not isinstance(data.get("web", {}), dict):
            raise SourceError("Поисковик вернул неизвестный формат JSON")
        items = data.get("web", {}).get("results", [])
    else:
        raw = client.fetch("https://www.bing.com/search?" + urlencode({"q": query, "format": "rss"}))
        if b"<!doctype" in raw.lower() or b"<!entity" in raw.lower():
            raise SourceError("Поисковик вернул не поддерживаемую RSS-страницу или проверку доступа")
        try:
            root = ET.fromstring(raw)
        except ET.ParseError:
            raise SourceError("RSS-поиск недоступен/заблокирован; можно настроить DATA_TOOLS_BRAVE_KEY") from None
        if root.tag != "rss":
            raise SourceError("Поисковик вернул не RSS; результаты не подтверждены")
        items = [{"title": item.findtext("title", ""), "url": item.findtext("link", ""),
                  "description": item.findtext("description", "")} for item in root.findall("./channel/item")]
    if not isinstance(items, list):
        raise SourceError("Поисковик вернул неизвестный формат результатов")
    source = graph.node("source", provider, identity=provider)
    graph.edge(graph.root, source, "поисковый запрос")
    count = 0
    for item in items[:limit]:
        if not isinstance(item, dict):
            continue
        url = safe_url(item.get("url", ""))
        if not url:
            continue
        node = graph.node("web_result", item.get("title") or url, url=url,
                          details={"Описание": item.get("description", ""), "Источник": provider,
                                   "Проверка": "Результат поискового индекса; содержимое сайта не проверялось",
                                   "Связь": "Совпадение текста/ника не доказывает общую личность"})
        graph.edge(source, node, "результат индекса")
        count += 1
    graph.source(provider + (" / соцсети" if social else ""), "ok" if count else "empty", count,
                 "Публичная выдача; охват и свежесть ограничены поисковым индексом. RSS не является гарантированным API")


class TelegramParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.meta: dict[str, str] = {}
        self.posts: list[dict] = []
        self.depth = 0
        self.current = None
        self.post_depth = 0
        self.text_depth = 0
        self.profile_depth = 0
        self.profile_title: list[str] = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        classes = attrs.get("class", "").split()
        if tag == "meta" and attrs.get("property") in ("og:title", "og:description"):
            self.meta[attrs["property"]] = attrs.get("content", "")
        if tag == "div":
            self.depth += 1
            if "tgme_page_title" in classes:
                self.profile_depth = self.depth
            if "tgme_widget_message" in classes and attrs.get("data-post"):
                self.current = {"post": attrs["data-post"], "text": [], "date": ""}
                self.post_depth = self.depth
            if "tgme_widget_message_text" in classes and self.current:
                self.text_depth = self.depth
        if tag == "br" and self.current and self.text_depth:
            self.current["text"].append("\n")
        if tag == "time" and self.current:
            self.current["date"] = attrs.get("datetime", "")

    def handle_data(self, data):
        if self.current and self.text_depth:
            self.current["text"].append(data)
        if self.profile_depth:
            self.profile_title.append(data)

    def handle_endtag(self, tag):
        if tag != "div":
            return
        if self.depth == self.text_depth:
            self.text_depth = 0
        if self.depth == self.profile_depth:
            self.profile_depth = 0
        if self.current and self.depth == self.post_depth:
            self.current["text"] = "".join(self.current["text"]).strip()
            self.posts.append(self.current)
            self.current = None
            self.text_depth = 0
        self.depth = max(0, self.depth - 1)


def telegram(graph: Graph, client: HttpClient, handle: str, words: str = "", limit: int = 20) -> None:
    handle = username(handle, telegram=True)
    url = "https://t.me/" + handle
    profile = TelegramParser()
    profile.feed(client.fetch(url).decode("utf-8", "replace"))
    title = "".join(profile.profile_title).strip()
    state = "Публичная страница получена" if title else "Статус аккаунта не подтверждён; Telegram может отдавать общую страницу"
    node = graph.node("telegram", title or "@" + handle, url=url,
                      details={"Username": "@" + handle, "Описание": profile.meta.get("og:description", ""),
                               "Статус": state, "Ограничение": "Только публичные данные. Никаких телефонов, закрытых чатов или установления личности"})
    graph.edge(graph.root, node, "проверен указанный username")
    preview = TelegramParser()
    try:
        suffix = "?" + urlencode({"q": words}) if words else ""
        preview.feed(client.fetch("https://t.me/s/" + handle + suffix).decode("utf-8", "replace"))
    except SourceError:
        graph.source("Telegram @" + handle, "partial", 1 if title else 0,
                     state + "; публичные публикации недоступны, профиль не считается несуществующим")
        return
    count = 0
    for post in preview.posts:
        match = re.fullmatch(r"([A-Za-z0-9_]+)/([0-9]+)", post["post"])
        if not match or match[1].casefold() != handle.casefold() or not post["text"]:
            continue
        if words and normalized(words) not in normalized(post["text"]):
            continue
        link = "https://t.me/" + post["post"]
        item = graph.node("telegram_post", post["text"][:90], url=link,
                          details={"Текст": post["text"], "Дата": post["date"], "Источник": "Публичный Telegram preview"})
        graph.edge(node, item, "публичная публикация")
        count += 1
        if count >= limit:
            break
    graph.source("Telegram @" + handle, "ok" if title or count else "unknown", count + bool(title),
                 state + "; только доступный preview, не полный архив Telegram и не глобальный поиск пользователей")


def github(graph: Graph, client: HttpClient, handle: str, limit: int = 20) -> None:
    handle = username(handle)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9-]{0,38}", handle):
        raise SourceError("Username не подходит для GitHub")
    headers = {"Authorization": "Bearer " + os.environ["DATA_TOOLS_GITHUB_TOKEN"]} if os.environ.get("DATA_TOOLS_GITHUB_TOKEN") else {}
    data = client.json("https://api.github.com/users/" + quote(handle), headers)
    if not isinstance(data, dict) or not data.get("login") or not data.get("html_url"):
        raise SourceError("GitHub вернул неизвестный формат профиля")
    account = graph.node("github", data["login"], url=data["html_url"], details={"Тип": data.get("type", ""),
                         "Публичное имя": data.get("name") or "", "Описание": data.get("bio") or "",
                         "Публичных репозиториев": data.get("public_repos", 0), "Примечание": "Ник не подтверждает общую личность с другими соцсетями"})
    graph.edge(graph.root, account, "публичный профиль")
    try:
        repos = client.json("https://api.github.com/users/" + quote(handle) + "/repos?" + urlencode({"type": "owner", "sort": "updated", "per_page": min(limit, 50)}), headers)
    except SourceError as exc:
        graph.source("GitHub @" + handle, "partial", 1, "Публичный профиль получен; репозитории недоступны: " + str(exc))
        return
    if not isinstance(repos, list):
        raise SourceError("GitHub вернул неизвестный формат списка репозиториев")
    count = 1
    for repo in repos[:limit]:
        if not isinstance(repo, dict) or repo.get("private"):
            continue
        item = graph.node("repository", repo.get("full_name", "repository"), url=repo.get("html_url", ""),
                          details={"Описание": repo.get("description") or "", "Язык": repo.get("language") or "",
                                   "Stars": repo.get("stargazers_count", 0), "Обновлено": repo.get("updated_at", "")})
        graph.edge(account, item, "публичный репозиторий")
        count += 1
    graph.source("GitHub @" + handle, "ok", count, "Публичные метаданные, без email/location/приватных репозиториев")


def social(graph: Graph, client: HttpClient, handle: str, limit: int = 20) -> None:
    handle = username(handle)
    for name, collect in (("GitHub", lambda: github(graph, client, handle, min(limit, 10))),
                          ("Telegram", lambda: telegram(graph, client, handle, limit=min(limit, 10))),
                          ("Социальный веб-поиск", lambda: search_web(graph, client, handle, limit, social=True))):
        try:
            collect()
        except SourceError as exc:
            graph.source(name, "error", 0, str(exc))
    # Explicitly mark these as links to check, NOT as discovered accounts.
    for platform, url in (("VK", f"https://vk.com/{handle}"), ("Instagram", f"https://www.instagram.com/{handle}/"),
                          ("TikTok", f"https://www.tiktok.com/@{handle}"), ("X", f"https://x.com/{handle}"),
                          ("YouTube", f"https://www.youtube.com/@{handle}")):
        node = graph.node("candidate", platform + " / " + handle, url=url,
                          details={"Статус": "НЕ ПРОВЕРЕНО — шаблон ссылки, не найденный аккаунт", "Причина": "Сайт может требовать вход/проверку доступа. Существование и принадлежность не установлены"})
        graph.edge(graph.root, node, "кандидат для ручной проверки")


def local_files(graph: Graph, paths: list[Path], words: str, limit: int = 20) -> None:
    for path in paths:
        if not path.is_file() or path.stat().st_size > 20_000_000:
            raise SourceError("Локальный файл отсутствует или превышает 20 МБ")
        suffix = path.suffix.lower()
        if suffix not in (".csv", ".json", ".jsonl"):
            raise SourceError("Поддерживаются CSV, JSON-массив и JSONL с публичными/разрешёнными записями")
        source = graph.node("local_file", path.name, identity=str(path.resolve()),
                            details={"Импорт": "Только разрешённые поля. Неизвестные колонки/секреты не импортируются"})
        graph.edge(graph.root, source, "поиск по разрешённому файлу")
        try:
            with path.open(encoding="utf-8-sig", newline="") as stream:
                if suffix == ".csv":
                    rows = csv.DictReader(stream)
                elif suffix == ".jsonl":
                    rows = (json.loads(line) for line in stream if line.strip())
                else:
                    rows = json.load(stream)
                    if not isinstance(rows, list):
                        raise ValueError("expected array")
                count = scanned = 0
                for index, row in enumerate(rows):
                    if index >= 100_000:
                        break
                    scanned += 1
                    if not isinstance(row, dict):
                        continue
                    public = public_record(row)
                    if not public or normalized(words) not in normalized(" ".join(public.values())):
                        continue
                    title = public.get("title") or public.get("name") or public.get("username") or f"Запись {index + 1}"
                    node = graph.node("local_record", title, identity=f"{source}:{index}", details=public,
                                      url=public.get("url", public.get("website", "")))
                    graph.edge(source, node, "совпадение разрешённых полей")
                    count += 1
                    if count >= limit:
                        break
        except (ValueError, UnicodeError, csv.Error, RecursionError):
            raise SourceError("Некорректный CSV/JSON/JSONL или кодировка не UTF-8") from None
        graph.source("Локальный файл: " + path.name, "ok" if count else "empty", count, f"Просмотрено записей: {scanned}. Результаты ограничены {limit}")
