#!/usr/bin/env python3
"""Point the website's download links at the newest GitHub release.

Every <a data-asset="pattern"> gets the download URL of the release asset whose name matches the pattern
(fnmatch), and <span data-version></span> becomes the tag. Without a release the links keep pointing at
releases/latest. Usage: update_site.py <site dir> <owner/repo>   (GITHUB_TOKEN is used when set)
"""
import fnmatch, json, os, re, sys, urllib.error, urllib.request

site, repo = sys.argv[1], sys.argv[2]
req = urllib.request.Request(f"https://api.github.com/repos/{repo}/releases/latest", headers={"Accept": "application/vnd.github+json"})
if os.environ.get("GITHUB_TOKEN"):
    req.add_header("Authorization", "Bearer " + os.environ["GITHUB_TOKEN"])
try:
    release = json.load(urllib.request.urlopen(req))
except urllib.error.HTTPError as e:
    if e.code != 404: raise
    print("No release yet: the links keep pointing at releases/latest")
    sys.exit(0)

assets = {a["name"]: a["browser_download_url"] for a in release["assets"]}
path = os.path.join(site, "index.html")
html = open(path, encoding="utf-8").read()

def link(m):
    tag, pattern = m.group(0), m.group(1)
    hit = next((u for n, u in sorted(assets.items()) if fnmatch.fnmatch(n, pattern)), None)
    if hit is None:
        print("no asset for", pattern)
        return tag
    return re.sub(r'href="[^"]*"', f'href="{hit}"', tag, count=1)

html = re.sub(r'<a [^>]*data-asset="([^"]+)"[^>]*>', link, html)
html = re.sub(r"<span data-version>[^<]*</span>", f"<span data-version>Version {release['tag_name'].lstrip('v')}.</span>", html)
open(path, "w", encoding="utf-8").write(html)
print("Links updated for", release["tag_name"])
