import contextlib
import io
import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from data_tools.core import Graph, HttpClient, SourceError, atomic_write, public_record, redact, safe_url
from data_tools.cli import main, render, demo_graph
from data_tools.licensing import KeyAuth, LicenseError, verify_response, getchecksum, device_id, APP_NAME, OWNER_ID, APP_VERSION
from data_tools.sources import TelegramParser, telegram, search_web, local_files, social, github, username

PROFILE = '<html><meta property="og:description" content="Open research"><div class="tgme_page_title"><span>Research channel</span></div></html>'
POSTS = '''<div class="tgme_widget_message" data-post="research_demo/12"><div class="tgme_widget_message_text">Python <b>graph</b><br>Release notes</div><time datetime="2026-10-15T00:00:00Z"></time></div>
<div class="tgme_widget_message" data-post="other_channel/13"><div class="tgme_widget_message_text">Python unrelated</div></div>'''


class FakeClient:
    def fetch(self, url, headers=None):
        if '/s/research_demo' in url:
            return POSTS.encode()
        if url == 'https://t.me/research_demo':
            return PROFILE.encode()
        if 'bing.com' in url:
            return b'<rss><channel><item><title>Public docs</title><link>https://example.org/docs</link><description>Python graph</description></item></channel></rss>'
        raise SourceError('fixture unavailable')

    def json(self, url, headers=None):
        if '/repos?' in url:
            return [{'full_name': 'research-demo/public', 'html_url': 'https://github.com/research-demo/public', 'private': False}]
        if '/users/research-demo' in url:
            return {'login': 'research-demo', 'html_url': 'https://github.com/research-demo', 'email': 'must-not-import@example.org', 'location': 'must-not-import', 'public_repos': 1}
        raise SourceError('fixture unavailable')


class CoreTests(unittest.TestCase):
    def test_graph_deduplicates(self):
        graph = Graph('test')
        a = graph.node('source', 'one')
        self.assertEqual(a, graph.node('source', 'one'))
        graph.edge(graph.root, a, 'public')
        graph.edge(graph.root, a, 'public')
        self.assertEqual(len(graph.edges), 1)
        with self.assertRaises(ValueError):
            graph.edge('missing', a, 'bad')

    def test_secrets_and_fields(self):
        value = redact('email me a@example.org, +1 202 555 0123 token=top-secret')
        self.assertNotIn('a@example.org', value)
        self.assertNotIn('top-secret', value)
        self.assertNotIn('555 0123', value)
        self.assertEqual(public_record({'title': 'public', 'password': 'no', 'phone': 'no', 'address': 'no'}), {'title': 'public'})
        for url in ['javascript:alert(1)', 'file:///tmp/a', 'https://user:secret@example.org/', 'https://example.org/a@example.org']:
            self.assertFalse(safe_url(url))
        self.assertEqual(safe_url('https://example.org/docs?token=secret#part'), 'https://example.org/docs')

    def test_safe_embedded_json(self):
        graph = Graph('</script><img src=x onerror=alert(1)>')
        html = render(graph.export())
        body = html.split('<script id="report-data" type="application/json">')[1].split('</script>')[0]
        self.assertNotIn('<', body)
        self.assertEqual(json.loads(body)['query'], graph.query)
        self.assertNotIn('https://cdn.', html)
        self.assertIn("connect-src 'none'", html)

    def test_output_exclusive_and_force(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'out.html'
            atomic_write(path, 'first')
            with self.assertRaises(FileExistsError):
                atomic_write(path, 'second')
            self.assertEqual(path.read_text(), 'first')
            atomic_write(path, 'second', force=True)
            self.assertEqual(path.read_text(), 'second')

    def test_http_host_guard(self):
        for url in ['http://api.github.com/users/a', 'https://localhost/', 'https://user:pass@api.github.com/a']:
            with self.assertRaises(SourceError):
                HttpClient().fetch(url)


class SourceTests(unittest.TestCase):
    def test_usernames(self):
        self.assertEqual(username('@research_demo', telegram=True), 'research_demo')
        for value in ['https://t.me/a', '../a', '+invite', 'a b', '']:
            with self.assertRaises(SourceError):
                username(value, telegram=True)

    def test_telegram_only_public_matching_posts(self):
        graph = Graph('research_demo')
        telegram(graph, FakeClient(), '@research_demo', 'Python', 10)
        self.assertEqual(len([n for n in graph.nodes.values() if n['kind'] == 'telegram_post']), 1)
        post = next(n for n in graph.nodes.values() if n['kind'] == 'telegram_post')
        self.assertEqual(post['url'], 'https://t.me/research_demo/12')
        self.assertIn('Python graph\nRelease notes', post['details']['Текст'])

    def test_telegram_generic_page_not_verified(self):
        class Empty:
            def fetch(self, *args):
                return b'<html>Contact a user</html>'
        graph = Graph('research_demo')
        telegram(graph, Empty(), 'research_demo')
        self.assertEqual(graph.sources[0]['state'], 'unknown')

    def test_rss(self):
        with patch.dict(os.environ, {}, clear=True):
            graph = Graph('Python')
            search_web(graph, FakeClient(), 'Python')
            self.assertEqual(graph.sources[0]['count'], 1)
            self.assertTrue(any(n['kind'] == 'web_result' for n in graph.nodes.values()))

    def test_github_no_email_or_location(self):
        graph = Graph('research-demo')
        github(graph, FakeClient(), 'research-demo')
        serialized = json.dumps(graph.export())
        self.assertNotIn('must-not-import', serialized)
        self.assertEqual(graph.sources[0]['count'], 2)

    def test_local_sensitive_columns_not_searched(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'public.csv'
            path.write_text('title,password\nResearch,secretneedle\n', encoding='utf-8')
            graph = Graph('secretneedle')
            local_files(graph, [path], 'secretneedle')
            self.assertEqual(graph.sources[0]['count'], 0)
            graph = Graph('Research')
            local_files(graph, [path], 'Research')
            self.assertEqual(graph.sources[0]['count'], 1)
            self.assertNotIn('secretneedle', json.dumps(graph.export()))

    def test_social_candidates_not_found_accounts(self):
        graph = Graph('research_demo')
        with patch.dict(os.environ, {}, clear=True):
            social(graph, FakeClient(), 'research_demo')
        candidates = [n for n in graph.nodes.values() if n['kind'] == 'candidate']
        self.assertEqual(len(candidates), 5)
        self.assertTrue(all('НЕ ПРОВЕРЕНО' in n['details']['Статус'] for n in candidates))


class LicenseTests(unittest.TestCase):
    def test_signature_timestamp_and_tamper(self):
        from nacl.signing import SigningKey
        key = SigningKey.generate()
        public = key.verify_key.encode().hex()
        now = int(time.time())
        body = b'{"success":true,"sessionid":"test"}'
        timestamp = str(now)
        signature = key.sign(timestamp.encode() + body).signature.hex()
        self.assertTrue(verify_response(body, signature, timestamp, key=public, now=now)['success'])
        for invalid_body, sig, stamp, clock in [(body + b' ', signature, timestamp, now), (body, '', timestamp, now),
                                               (body, signature, timestamp, now + 26), (body, signature, timestamp, now - 26)]:
            with self.assertRaises(LicenseError):
                verify_response(invalid_body, sig, stamp, key=public, now=clock)
        with self.assertRaises(LicenseError):
            verify_response(body, signature, timestamp, now=now)  # test signer isn't the production vendor

    def test_app_config(self):
        self.assertEqual((APP_NAME, OWNER_ID, APP_VERSION), ('Data-Tools', '2bTkDP2v6c', '1.0'))
        self.assertEqual(len(getchecksum()), 32)

    def test_device_id_is_local_and_stable(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {'XDG_CONFIG_HOME': directory}):
            first = device_id()
            self.assertEqual(first, device_id())
            self.assertEqual((Path(directory) / 'data-tools/device-id').stat().st_mode & 0o777, 0o600)

    def test_auth_fail_closed(self):
        auth = KeyAuth()
        with patch.object(auth, '_request', return_value={'success': True, 'sessionid': 'session'}), patch('data_tools.licensing.device_id', return_value='test-install'):
            auth.authenticate('test-license')
            self.assertTrue(auth.authorized)
        with patch.object(auth, '_request', side_effect=LicenseError('denied')):
            with self.assertRaises(LicenseError):
                auth.authenticate('test-license')
        self.assertFalse(auth.authorized)
        self.assertEqual(auth.session, '')

    def test_cli_does_not_search_on_license_failure(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {'DATA_TOOLS_LICENSE': 'test-only'}), \
             patch('data_tools.cli.KeyAuth.authenticate', side_effect=LicenseError('denied')), \
             patch('data_tools.cli.telegram') as source, contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            path = Path(directory) / 'report.html'
            self.assertEqual(main(['telegram', 'research_demo', '-o', str(path)]), 3)
            source.assert_not_called()
            self.assertFalse(path.exists())

    def test_demo_never_authenticates(self):
        with tempfile.TemporaryDirectory() as directory, patch('data_tools.cli.KeyAuth.authenticate') as auth, \
             contextlib.redirect_stdout(io.StringIO()):
            path = Path(directory) / 'demo.html'
            self.assertEqual(main(['demo', '-o', str(path)]), 0)
            auth.assert_not_called()
            self.assertIn('ДЕМО', path.read_text())


class RegressionTests(unittest.TestCase):
    def test_malformed_brave_shapes(self):
        class Malformed:
            def json(self, *args):
                return {"web": {"results": {"wrong": "shape"}}}
        with patch.dict(os.environ, {'DATA_TOOLS_BRAVE_KEY': 'fixture-only'}):
            with self.assertRaises(SourceError):
                search_web(Graph('test'), Malformed(), 'test')

    def test_github_partial_profile_retained(self):
        class Partial(FakeClient):
            def json(self, url, headers=None):
                if '/repos?' in url:
                    raise SourceError('unavailable')
                return super().json(url, headers)
        graph = Graph('research-demo')
        github(graph, Partial(), 'research-demo')
        self.assertEqual(graph.sources[0]['state'], 'partial')
        self.assertEqual(graph.sources[0]['count'], 1)
        self.assertTrue(any(n['kind'] == 'github' for n in graph.nodes.values()))

    def test_overflow_timestamp_denied(self):
        with self.assertRaises(LicenseError):
            verify_response(b'{}', '00', '9' * 400)

    def test_unsigned_transport_denied(self):
        class Response:
            headers = {}
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, limit): return b'{"success":true,"sessionid":"fake"}'
        auth = KeyAuth()
        with patch.object(auth.opener, 'open', return_value=Response()):
            with self.assertRaises(LicenseError):
                auth.authenticate('test-only')
        self.assertFalse(auth.authorized)

    def test_cli_input_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as directory, patch('data_tools.cli.KeyAuth.authenticate') as auth, \
             contextlib.redirect_stderr(io.StringIO()):
            path = Path(directory) / 'records.csv'
            path.write_text('title\nResearch\n')
            self.assertEqual(main(['local', 'Research', str(path), '--authorized', '--force', '-o', str(path)]), 2)
            auth.assert_not_called()
            self.assertEqual(path.read_text(), 'title\nResearch\n')

    def test_hidden_input_eof(self):
        with patch.dict(os.environ, {}, clear=True), patch('sys.stdin.isatty', return_value=True), \
             patch('data_tools.cli.getpass.getpass', side_effect=EOFError), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main(['auth-check']), 130)


if __name__ == '__main__':
    unittest.main()
