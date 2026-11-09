"""Read a registry manifest digest; only a missing or refused tag permits a new image build."""

import base64
import hashlib
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass

ACCEPT = ("application/vnd.oci.image.index.v1+json, application/vnd.oci.image.manifest.v1+json, "
          "application/vnd.docker.distribution.manifest.list.v2+json, "
          "application/vnd.docker.distribution.manifest.v2+json")


@dataclass(frozen=True)
class Response:
    status: int
    body: bytes
    headers: dict[str, str]


def request(url: str, headers: dict[str, str], allowed: tuple[int, ...] = (200,)) -> Response:
    """Retry transient failures with a bounded timeout; authentication failures stay failures."""
    for attempt in range(3):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=30) as response:
                return Response(response.status, response.read(), {k.lower(): v for k, v in response.headers.items()})
        except urllib.error.HTTPError as error:
            with error:
                if error.code in allowed:
                    return Response(error.code, error.read(), {k.lower(): v for k, v in error.headers.items()})
                if error.code not in (408, 429, 500, 502, 503, 504) or attempt == 2:
                    raise
        except (urllib.error.URLError, TimeoutError):
            if attempt == 2:
                raise
        time.sleep(2 ** attempt)
    raise RuntimeError("Registry retry loop exhausted")


def lookup(ref: str, username: str, token: str) -> str | None:
    registry, path = ref.split("/", 1)
    repository, tag = path.rsplit(":", 1)
    root = request(f"https://{registry}/v2/", {}, (200, 401))
    headers = {"Accept": ACCEPT}
    challenge = root.headers.get("www-authenticate", "")
    if root.status == 401:
        if not challenge.lower().startswith("bearer "):
            raise ValueError("Registry did not offer Bearer authentication")
        fields = dict(re.findall(r'(\w+)="([^"]+)"', challenge))
        realm = fields["realm"]
        if not realm.startswith("https://"):
            raise ValueError("Registry authentication requires HTTPS")
        query = urllib.parse.urlencode({"service": fields.get("service", registry),
                                        "scope": f"repository:{repository}:pull"})
        auth = base64.b64encode(f"{username}:{token}".encode()).decode()
        response = request(realm + ("&" if "?" in realm else "?") + query, {"Authorization": "Basic " + auth})
        payload = json.loads(response.body)
        bearer = payload.get("token") or payload.get("access_token")
        if not isinstance(bearer, str) or not bearer:
            raise ValueError("Registry authentication returned no token")
        headers["Authorization"] = "Bearer " + bearer
    # A registry may refuse a package that does not exist yet (403) as it refuses one the token
    # cannot read; only the push can tell them apart, so either way the image gets built.
    manifest = request(f"https://{registry}/v2/{repository}/manifests/{tag}", headers, (200, 403, 404))
    return None if manifest.status != 200 else "sha256:" + hashlib.sha256(manifest.body).hexdigest()


if __name__ == "__main__":
    try:
        print(lookup(sys.argv[1], os.environ["GITHUB_REPOSITORY_OWNER"], os.environ["TOKEN"]) or "")
    except (OSError, ValueError, KeyError) as error:
        sys.exit(f"Image lookup failed ({type(error).__name__}); refusing to treat it as a missing image")
