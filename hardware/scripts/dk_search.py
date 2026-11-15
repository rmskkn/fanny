#!/usr/bin/env python3
"""DigiKey cheapest-candidate search for a list of BOM line items.

Report-only: prints ranked candidates per line, does not touch bom.csv.
Input: JSON list of {"ref": "D1", "keywords": "SS54 schottky SMB",
"package": "SMB", "qty": 1}. "package" and "qty" are optional; "qty"
defaults to --qty (prototype quantity, used to pick the lowest-MOQ
variation per candidate, not just the lowest headline price).

Add "nominal" to verify the match instead of trusting keyword-search rank:
- "component_type": "resistor" or "capacitor" -> nominal is an engineering
  value ("100", "4.7k", "100n"), checked against DigiKey's own
  Resistance/Capacitance parameter. Bare numeric keyword searches (e.g.
  "100 resistor 0603") routinely rank wrong-value parts (e.g. 1k0) above
  the real 100R match.
- any other/no component_type -> if nominal looks like a part number
  (has both letters and digits, e.g. "SS54", "1N4148WT", "BSS138"), it is
  checked as a prefix match against the candidate's ManufacturerProductNumber.
  Covers diodes, transistors, ICs, and anything else whose schematic Value
  field is already the base MPN. A nominal that isn't MPN-shaped (a color,
  a connector label) is left unverified rather than guessed at.

Auth: DIGIKEY_CLIENT_ID / DIGIKEY_CLIENT_SECRET env vars, or
~/.config/secrets.env (same two names).
"""
import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

_PREFIX = {"p": 1e-12, "n": 1e-9, "u": 1e-6, "µ": 1e-6, "m": 1e-3, "k": 1e3, "K": 1e3, "M": 1e6}


def parse_engineering(text):
    """"4.7k" -> 4700.0, "100n" -> 1e-7, "1 kOhms" -> 1000.0, "100 Ohms" -> 100.0."""
    m = re.match(r"^\s*([\d.]+)\s*([a-zA-Zµ]*)", text or "")
    if not m:
        return None
    num = float(m.group(1))
    unit = m.group(2)
    if not unit:
        return num
    prefix = unit[0]
    rest = unit[1:].lower()
    if prefix in _PREFIX and (rest == "" or rest in ("ohms", "ohm", "f", "h")):
        return num * _PREFIX[prefix]
    return num  # bare unit (Ohms, F) with no prefix letter recognized

API = "https://api.digikey.com"


def load_creds():
    cid = os.environ.get("DIGIKEY_CLIENT_ID")
    csec = os.environ.get("DIGIKEY_CLIENT_SECRET")
    if cid and csec:
        return cid, csec
    secrets_path = Path.home() / ".config" / "secrets.env"
    if secrets_path.exists():
        for line in secrets_path.read_text().splitlines():
            if line.startswith("DIGIKEY_CLIENT_ID="):
                cid = line.split("=", 1)[1].strip()
            elif line.startswith("DIGIKEY_CLIENT_SECRET="):
                csec = line.split("=", 1)[1].strip()
    if not (cid and csec):
        sys.exit("DIGIKEY_CLIENT_ID / DIGIKEY_CLIENT_SECRET not set (env or ~/.config/secrets.env)")
    return cid, csec


def http(url, headers, body=None, method="GET"):
    req = urllib.request.Request(url, data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


def get_token(cid, csec):
    body = f"client_id={cid}&client_secret={csec}&grant_type=client_credentials".encode()
    status, resp = http(f"{API}/v1/oauth2/token",
                         {"Content-Type": "application/x-www-form-urlencoded"}, body, "POST")
    if status != 200:
        sys.exit(f"token request failed: {status} {resp}")
    return resp["access_token"]


def keyword_search(token, cid, keywords, limit):
    body = json.dumps({
        "Keywords": keywords,
        "Limit": limit,
        "FilterOptionsRequest": {"MarketPlaceFilter": "ExcludeMarketPlace", "SearchOptions": ["InStock"]},
        "SortOptions": {"Field": "QuantityAvailable", "SortOrder": "Descending"},
    }).encode()
    headers = {"Authorization": f"Bearer {token}", "X-DIGIKEY-Client-Id": cid, "Content-Type": "application/json"}
    status, data = http(f"{API}/products/v4/search/keyword", headers, body, "POST")
    if status != 200:
        print(f"  search failed ({status}): {str(data)[:200]}", file=sys.stderr)
        return []
    return data.get("Products", [])


def best_variation(product):
    """Variation with the lowest MinimumOrderQuantity (most prototype-friendly)."""
    variations = product.get("ProductVariations") or []
    if not variations:
        return None
    return min(variations, key=lambda v: v.get("MinimumOrderQuantity") or 10**9)


def tier_price(variation):
    pricing = variation.get("StandardPricing") or []
    if not pricing:
        return None
    return min(p["UnitPrice"] for p in pricing)


def package_of(product):
    for p in product.get("Parameters", []):
        if p.get("ParameterText") == "Package / Case":
            return p.get("ValueText")
    return None


def param_value(product, param_name):
    for p in product.get("Parameters", []):
        if p.get("ParameterText") == param_name:
            return p.get("ValueText")
    return None


PARAM_BY_TYPE = {"resistor": "Resistance", "capacitor": "Capacitance"}


def norm_mpn(s):
    return re.sub(r"[^A-Z0-9]", "", (s or "").upper())


def looks_like_mpn(s):
    return bool(re.search(r"[A-Za-z]", s)) and bool(re.search(r"\d", s)) and len(s) >= 3


def mpn_ok(nominal, product):
    n, a = norm_mpn(nominal), norm_mpn(product.get("ManufacturerProductNumber"))
    if not n or not a:
        return False
    return a.startswith(n) or n.startswith(a)


def value_ok(component_type, nominal, product):
    if not nominal:
        return None
    param_name = PARAM_BY_TYPE.get(component_type)
    if param_name:
        actual_text = param_value(product, param_name)
        expected = parse_engineering(nominal)
        actual = parse_engineering(actual_text) if actual_text else None
        if expected is None or actual is None:
            return False
        return abs(actual - expected) <= 0.02 * expected
    if looks_like_mpn(nominal):
        return mpn_ok(nominal, product)
    return None


def candidate_row(product):
    var = best_variation(product)
    package = package_of(product)
    return {
        "mpn": product.get("ManufacturerProductNumber"),
        "mfr": (product.get("Manufacturer") or {}).get("Name"),
        "dkpn": var.get("DigiKeyProductNumber") if var else None,
        "moq": var.get("MinimumOrderQuantity") if var else None,
        "price": tier_price(var) if var else None,
        "qty_avail": product.get("QuantityAvailable"),
        "status": (product.get("ProductStatus") or {}).get("Status"),
        "package": package,
        "url": product.get("ProductUrl"),
    }


def package_ok(expected, actual):
    if not expected:
        return None
    if not actual:
        return False
    return expected.lower() in actual.lower()


def run(specs, default_qty, limit):
    cid, csec = load_creds()
    token = get_token(cid, csec)

    for spec in specs:
        ref = spec.get("ref", "?")
        keywords = spec["keywords"]
        package = spec.get("package")
        nominal = spec.get("nominal")
        component_type = spec.get("component_type")
        qty = spec.get("qty", default_qty)
        print(f"\n=== {ref}  \"{keywords}\"  package={package or '-'}  nominal={nominal or '-'}  qty={qty} ===")

        products = keyword_search(token, cid, keywords, limit)
        if not products:
            print("  no in-stock results")
            continue

        rows = []
        for p, row in zip(products, (candidate_row(p) for p in products)):
            row["pkg_ok"] = package_ok(package, row["package"])
            row["val_ok"] = value_ok(component_type, nominal, p)
            rows.append(row)
        rows = [r for r in rows if r["price"] is not None]
        # wrong-value candidates (val_ok is False, i.e. verified mismatch) sort last,
        # regardless of price — a cheaper wrong part is not a cheaper match.
        rows.sort(key=lambda r: (r["val_ok"] is False, r["price"]))

        if not rows:
            print("  no candidate had resolvable pricing")
            continue

        for i, r in enumerate(rows):
            mark = "BEST" if i == 0 and r["val_ok"] is not False else "    "
            flags = []
            if r["pkg_ok"] is False:
                flags.append("package mismatch?")
            if r["val_ok"] is False:
                flags.append("VALUE MISMATCH")
            flag = f"  [{', '.join(flags)}]" if flags else ""
            print(f"  {mark}  {str(r['dkpn']):<28} {str(r['mpn']):<20} {str(r['mfr']):<28} "
                  f"${r['price']:<9.4f} moq={str(r['moq']):<6} stock={str(r['qty_avail']):<8} "
                  f"pkg={r['package']}{flag}")
            print(f"        {r['url']}")
        if all(r["val_ok"] is False for r in rows):
            print("  WARNING: no candidate's Resistance/Capacitance parameter matched the nominal — "
                  "none of these are the right part, refine keywords")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("specs_json", help="path to JSON list of {ref, keywords, package?, qty?}")
    ap.add_argument("--qty", type=int, default=1, help="default prototype quantity (per line, unless overridden)")
    ap.add_argument("--limit", type=int, default=10, help="max candidates fetched per line")
    args = ap.parse_args()

    specs = json.loads(Path(args.specs_json).read_text())
    run(specs, args.qty, args.limit)


if __name__ == "__main__":
    main()
