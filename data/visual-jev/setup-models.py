"""Download pinned public model files and verify their published LFS hashes."""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import urllib.request

REPOS = [("Qwen/Qwen3-VL-4B-Instruct", "ebb281ec70b05090aa6165b016eac8ec08e71b17", "base"),
         ("guanxuyu/visual-jev-4b-answer-sft", "7a3f1bb0d8f7bfbbd8514d968ab7de3518849c8d", "adapter")]

def download(item):
    repo, revision, sibling, target = item
    name = sibling["rfilename"]
    path = target / name
    path.parent.mkdir(parents=True, exist_ok=True)
    expected = sibling.get("lfs", {}).get("sha256")
    expected_size = sibling.get("size")
    if not path.exists() or (expected_size and path.stat().st_size != expected_size):
        print("Downloading", repo, name, flush=True)
        url = f"https://huggingface.co/{repo}/resolve/{revision}/{name}?download=true"
        partial = path.with_suffix(path.suffix + ".part")
        with urllib.request.urlopen(url, timeout=120) as response, partial.open("wb") as stream:
            while chunk := response.read(8 * 1024 * 1024): stream.write(chunk)
        partial.replace(path)
    if expected:
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            while chunk := stream.read(8 * 1024 * 1024): digest.update(chunk)
        if digest.hexdigest() != expected: raise ValueError(f"Checksum mismatch: {name}")
    print("Verified", name, flush=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime", required=True)
    parser.add_argument("--profile", required=True)
    args = parser.parse_args()
    root = Path(args.runtime).resolve()
    profile = Path(args.profile).resolve()
    items = []
    revisions = {}
    for repo, revision, directory in REPOS:
        with urllib.request.urlopen(f"https://huggingface.co/api/models/{repo}/revision/{revision}?blobs=true",
                                    timeout=60) as response:
            metadata = json.load(response)
        if metadata["sha"] != revision: raise ValueError("Unexpected model revision")
        revisions[repo] = revision
        for sibling in metadata["siblings"]:
            name = sibling["rfilename"]
            if "/" in name or name.startswith(".") or name.endswith(".gif"): continue
            items.append((repo, revision, sibling, root / "models" / directory))
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(download, items))
    config = {"schema": 1, "pythonPath": str(root / "venv/Scripts/python.exe"),
        "modelDirectory": str(root / "models/base"), "adapterDirectory": str(root / "models/adapter"),
        "batchSize": 4, "maxPixels": 401408, "revisions": revisions}
    profile.mkdir(parents=True, exist_ok=True)
    temporary = profile / "local-runtime.json.tmp"
    temporary.write_text(json.dumps(config, indent=2), encoding="utf-8")
    temporary.replace(profile / "local-runtime.json")
    print("QUICKSCORE_LOCAL_READY", flush=True)

if __name__ == "__main__": main()
