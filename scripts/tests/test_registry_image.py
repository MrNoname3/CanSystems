"""Registry failures must not trigger a costly toolchain rebuild."""

import io
import sys
import urllib.error
from email.message import Message
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import registry_image as registry


@pytest.mark.parametrize("status", [403, 404])
def test_a_missing_or_refused_manifest_is_a_cache_miss(monkeypatch: object, status: int) -> None:
    def request(url: str, headers: dict[str, str], allowed: tuple[int, ...]) -> registry.Response:
        if "/manifests/" in url:
            assert status in allowed
            return registry.Response(status, b"", {})
        return registry.Response(200, b"", {})

    monkeypatch.setattr(registry, "request", request)   # type: ignore[attr-defined]
    assert registry.lookup("registry.example.invalid/owner/image:tag", "owner", "unused") is None


@pytest.mark.parametrize("status, attempts", [(401, 1), (403, 1), (500, 3), (429, 3)])
def test_errors_are_bounded_and_propagated(monkeypatch: object, status: int, attempts: int) -> None:
    called = 0

    def fail(*args: object, **kwargs: object) -> None:
        nonlocal called
        called += 1
        raise urllib.error.HTTPError("https://registry.example.invalid", status, "failed", Message(), io.BytesIO())

    monkeypatch.setattr(registry.urllib.request, "urlopen", fail)   # type: ignore[attr-defined]
    monkeypatch.setattr(registry.time, "sleep", lambda seconds: None)   # type: ignore[attr-defined]
    with pytest.raises(urllib.error.HTTPError):
        registry.request("https://registry.example.invalid", {}, (200, 404))
    assert called == attempts
