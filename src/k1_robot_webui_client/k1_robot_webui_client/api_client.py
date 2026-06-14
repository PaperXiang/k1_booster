import json
import urllib.error
import urllib.parse
import urllib.request


class WebuiApiClient:
    def __init__(self, server_base_url: str, timeout_sec: float):
        self.server_base_url = server_base_url.rstrip("/")
        self.timeout_sec = timeout_sec

    def post_json(self, path: str, payload: dict) -> tuple[bool, str]:
        url = f"{self.server_base_url}{path}"
        data = json.dumps(payload).encode("utf-8")
        request = urllib.request.Request(
            url,
            data=data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=self.timeout_sec) as response:
                response.read()
            return True, ""
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            return False, str(exc)


def quote_robot_id(robot_id: str) -> str:
    return urllib.parse.quote(robot_id, safe="")
