# Third-party dependencies and protocol references

## Runtime

- Python standard library — Python Software Foundation License: https://docs.python.org/3/license.html . Python is not bundled.
- **PyNaCl** — Apache License 2.0. https://github.com/pyca/pynacl . Installed through `requirements.txt`; no dependency source or binary is bundled in this project archive.
- PyNaCl pulls **cffi** (MIT) and may pull **pycparser** (BSD-3-Clause), depending on platform/Python version. https://github.com/python-cffi/cffi ; https://github.com/eliben/pycparser . PyNaCl uses **libsodium**, ISC license: https://github.com/jedisct1/libsodium . Consult installed distributions for full applicable notices, especially when redistributing wheels or binaries.

## KeyAuth interoperability reference

Independent minimal client in `data_tools/licensing.py`; the vendor Python SDK is **not vendored, imported or executed**.

Protocol/signing-key/checksum behavior was checked against:

- https://github.com/KeyAuth/KeyAuth-Python-Example
- Reference revision: `5f3894bab5e133e902222c04b0a98df149a41667`
- API endpoint: `https://keyauth.win/api/1.3/`
- Response header names: `x-signature-ed25519`, `x-signature-timestamp`
- Signature input: timestamp bytes followed by raw response body
- Public verification key: `5586b4bc69c7a4b487e4563a4cd96afd39140f919bd31cea7d1c6a1e8439422b`

The reference repository is under **Elastic License 2.0**. Its full SDK is not part of this distribution. The client here implements the required init/license HTTP exchange using Python's standard library and PyNaCl, with no SDK auto-install behavior. It rejects responses more than 25 seconds in the future as well as the past, and uses a local installation UUID rather than collecting hardware serials. These intentional differences can affect compatibility with pre-existing HWID bindings. Vendor endpoint/key/protocol may change; do not disable signature verification to work around errors.

KeyAuth is an external service. This project is not an official SDK and does not claim endorsement. Application identifiers were supplied by the project requester; no license key is bundled.

## Public-source services

Telegram, GitHub, Bing, Brave, VK, Instagram, TikTok, X and YouTube are third-party services/trademarks. Their availability, rate limits, index coverage, and terms apply. No service content is bundled in the demo; `examples/demo.html` and `examples/public_records.csv` use synthetic data and example.org links.

## Development only

`tests/test_ui.cjs` optionally uses **jsdom 26**, MIT license: https://github.com/jsdom/jsdom . Node.js/npm/jsdom and transitive dependencies are not runtime requirements or included in the ZIP. The HTML viewer uses native browser SVG/DOM APIs, with no external JavaScript libraries, fonts, or images.
