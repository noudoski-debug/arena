from __future__ import annotations

import argparse
import getpass
import json
import os
import sys
import warnings
from pathlib import Path

from .core import Graph, HttpClient, SourceError, atomic_write
from .licensing import KeyAuth, LicenseError, getchecksum
from .sources import github, local_files, search_web, social, telegram, username


def render(graph: dict) -> str:
    payload = json.dumps(graph, ensure_ascii=False).replace("&", "\\u0026").replace("<", "\\u003c").replace(">", "\\u003e").replace("\u2028", "\\u2028").replace("\u2029", "\\u2029")
    template = Path(__file__).with_name("report.html").read_text(encoding="utf-8")
    return template.replace("__REPORT_DATA__", payload, 1)


def demo_graph() -> dict:
    graph = Graph("aurora-lab · публичные источники")
    search = graph.node("source", "Демонстрационный веб-поиск")
    graph.edge(graph.root, search, "поисковый запрос")
    web = graph.node("web_result", "Aurora Lab / Open research", url="https://example.org/aurora",
                     details={"Описание": "Вымышленный проект для демонстрации интерфейса. Реального поиска не было."})
    graph.edge(search, web, "публичная страница")
    git = graph.node("github", "aurora-lab-demo", url="https://example.org/github/aurora-lab-demo",
                     details={"Статус": "ВЫМЫШЛЕННЫЙ АККАУНТ", "Тип": "Организация", "Проверка": "Учебные данные, не результат запроса GitHub"})
    graph.edge(graph.root, git, "демонстрационная связь")
    for name, language, stars in [("field-notes", "Python", 218), ("signal-atlas", "TypeScript", 94), ("open-datasets", "CSV", 61)]:
        repo = graph.node("repository", name, url="https://example.org/repo/" + name,
                          details={"Язык": language, "Stars": stars, "Статус": "Синтетический пример"})
        graph.edge(git, repo, "публичный репозиторий")
    tg = graph.node("telegram", "Aurora / Research log", url="https://example.org/telegram/aurora",
                    details={"Username": "@aurora_demo", "Статус": "Вымышленный канал", "Описание": "Полевые заметки и открытые исследования"})
    graph.edge(graph.root, tg, "публичный preview")
    for index, title in enumerate(["Публикация открытого набора данных", "Заметки о построении графа", "Новая версия проекта"]):
        post = graph.node("telegram_post", title, url=f"https://example.org/telegram/post/{index}", details={"Текст": title, "Статус": "Синтетический пример"})
        graph.edge(tg, post, "публичная публикация")
    candidate = graph.node("candidate", "Соцсеть / aurora_demo", url="https://example.org/profile/aurora_demo",
                           details={"Статус": "НЕ ПРОВЕРЕНО", "Примечание": "Ник не доказывает существование аккаунта или общую личность"})
    graph.edge(graph.root, candidate, "кандидат для проверки")
    graph.source("Demo / веб-поиск", "ok", 1, "Только демонстрация интерфейса")
    graph.source("Demo / GitHub", "ok", 4, "Вымышленные репозитории")
    graph.source("Demo / Telegram", "ok", 4, "Вымышленные публикации")
    result = graph.export()
    result["demo"] = True
    return result


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(prog="Data-Tools", description="Публичный поиск → интерактивный автономный HTML-граф. Вход по лицензии KeyAuth.")
    commands = root.add_subparsers(dest="command", required=True)
    commands.add_parser("checksum", help="MD5 launcher для настройки hash в своей панели KeyAuth")
    commands.add_parser("auth-check", help="Проверить лицензию без поиска")
    demo = commands.add_parser("demo", help="Офлайн-демо на вымышленных данных; не обращается к источникам/KeyAuth")
    web = commands.add_parser("search", help="Поиск по словам в публичном поисковом индексе")
    web.add_argument("words")
    tg = commands.add_parser("telegram", help="Публичный Telegram username и доступные публикации")
    tg.add_argument("username")
    tg.add_argument("--words", default="", help="Фильтр текста публичных публикаций, не глобальный поиск Telegram")
    profiles = commands.add_parser("social", help="GitHub/Telegram + веб-поиск по соцсетям; непроверенные ссылки помечаются")
    profiles.add_argument("username")
    git = commands.add_parser("github", help="Публичный аккаунт и репозитории GitHub")
    git.add_argument("username")
    local = commands.add_parser("local", help="Поиск по разрешённым CSV/JSON/JSONL, не сервис утечек")
    local.add_argument("words")
    local.add_argument("files", type=Path, nargs="+")
    local.add_argument("--authorized", action="store_true", help="Подтверждаю право использовать указанные локальные данные")
    for sub in (demo, web, tg, profiles, git, local):
        sub.add_argument("-o", "--output", type=Path, default=Path("data-tools-report.html"))
        sub.add_argument("--json", type=Path, help="Дополнительный JSON-файл графа")
        sub.add_argument("--force", action="store_true", help="Разрешить замену существующих отчётов")
        if sub is not demo:
            sub.add_argument("--limit", type=int, default=20, choices=range(1, 51), metavar="1..50")
    return root


def main(argv=None) -> int:
    args = parser().parse_args(argv)
    try:
        if args.command == "checksum":
            print(getchecksum())
            return 0
        if args.command not in ("demo", "auth-check"):
            term = getattr(args, "words", "") or getattr(args, "username", "")
            if not term.strip() or len(term) > 240:
                raise SourceError("Введите запрос/username длиной 1–240 символов")
            if hasattr(args, "username"):
                username(args.username, telegram=args.command == "telegram")
            if args.command == "local" and (not args.authorized or len(args.files) > 10):
                raise SourceError("Нужен --authorized; не более 10 локальных файлов за запуск")
        if args.command != "auth-check":
            outputs = [args.output] + ([args.json] if args.json else [])
            if len({p.resolve() for p in outputs}) != len(outputs):
                raise SourceError("HTML и JSON должны записываться в разные файлы")
            inputs = {p.resolve() for p in getattr(args, "files", [])}
            for path in outputs:
                if path.resolve() in inputs:
                    raise SourceError("Нельзя перезаписывать входной файл отчётом")
                if path.exists() and not args.force:
                    raise SourceError("Отчёт уже существует; выберите другое имя или добавьте --force")
        if args.command != "demo":
            license_key = os.environ.get("DATA_TOOLS_LICENSE", "")
            if not license_key:
                if not sys.stdin.isatty():
                    raise LicenseError("Задайте DATA_TOOLS_LICENSE в окружении или запустите в терминале для скрытого ввода")
                try:
                    with warnings.catch_warnings():
                        warnings.simplefilter("error", getpass.GetPassWarning)
                        license_key = getpass.getpass("Лицензионный ключ KeyAuth: ")
                except getpass.GetPassWarning:
                    raise LicenseError("Скрытый ввод недоступен; используйте DATA_TOOLS_LICENSE в окружении") from None
            auth = KeyAuth()
            auth.authenticate(license_key)
            del license_key
            if not auth.authorized:
                raise LicenseError("Лицензия не подтверждена")
            print("KeyAuth: лицензия подтверждена.")
        if args.command == "auth-check":
            return 0
        if args.command == "demo":
            result = demo_graph()
        else:
            query = getattr(args, "username", None) or args.words
            graph = Graph(query)
            client = HttpClient()
            try:
                if args.command == "search":
                    search_web(graph, client, args.words, args.limit)
                elif args.command == "telegram":
                    telegram(graph, client, args.username, args.words, args.limit)
                elif args.command == "social":
                    social(graph, client, args.username, args.limit)
                elif args.command == "github":
                    github(graph, client, args.username, args.limit)
                elif args.command == "local":
                    local_files(graph, args.files, args.words, args.limit)
            except SourceError as exc:
                graph.source(args.command, "error", 0, str(exc))
            result = graph.export()
        atomic_write(args.output, render(result), force=args.force)
        if args.json:
            atomic_write(args.json, json.dumps(result, ensure_ascii=False, indent=2), force=args.force)
        print(f"HTML: {args.output}\nУзлов: {len(result['nodes'])}; связей: {len(result['edges'])}")
        errors = [source for source in result["sources"] if source["state"] == "error"]
        for source in errors:
            print(f"Источник {source['name']}: {source['note']}", file=sys.stderr)
        if errors and all(source["state"] == "error" for source in result["sources"]):
            print("Все источники завершились ошибкой; HTML содержит диагностику, а не результаты поиска.", file=sys.stderr)
            return 2
        return 0
    except LicenseError as exc:
        print("Ошибка лицензии: " + str(exc), file=sys.stderr)
        return 3
    except (SourceError, OSError) as exc:
        print("Ошибка: " + str(exc), file=sys.stderr)
        return 2
    except (KeyboardInterrupt, EOFError):
        print("\nОтменено.", file=sys.stderr)
        return 130
