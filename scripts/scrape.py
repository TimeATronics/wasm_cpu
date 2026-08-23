import os
import re
import requests
from urllib.parse import urljoin, urlparse
from bs4 import BeautifulSoup
import markdownify

# List your target URLs here (duplicates removed)
URLS = [
    "https://blog.ruux.de/tang-nano-9k-softcore-blink/",
    "https://blog.ruux.de/tang-nano-9k-softcore-blink-user-flash-method/"
]

# Directory structure
OUTPUT_DIR = "markdown_posts"
IMAGES_DIR = os.path.join(OUTPUT_DIR, "images")

os.makedirs(IMAGES_DIR, exist_ok=True)

HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
}

def sanitize_filename(name):
    """Clean string to create safe file names."""
    return re.sub(r'[\/:*?"<>|]', '_', name)

def download_image(img_url, save_dir, prefix_str=""):
    """Download an image and return its local relative path."""
    try:
        response = requests.get(img_url, headers=HEADERS, timeout=10)
        response.raise_for_status()

        # Extract filename from URL or fallback
        parsed_url = urlparse(img_url)
        base_filename = os.path.basename(parsed_url.path)
        if not base_filename or '.' not in base_filename:
            base_filename = f"img_{hash(img_url)}.jpg"
        
        # Scope image names by post prefix to prevent collisions
        filename = sanitize_filename(f"{prefix_str}{base_filename}")
        filepath = os.path.join(save_dir, filename)

        with open(filepath, "wb") as f:
            f.write(response.content)
            
        # Return relative path for Markdown reference
        return f"images/{filename}"
    except Exception as e:
        print(f"   [!] Failed to download image {img_url}: {e}")
        return img_url  # Keep original remote URL if download fails

def process_url(index, url):
    # Formats index as two-digit padded string: 00, 01, 02...
    prefix = f"{index:02d}_"
    print(f"Processing [{prefix.strip('_')}]: {url}")

    try:
        res = requests.get(url, headers=HEADERS, timeout=15)
        res.raise_for_status()
    except Exception as e:
        print(f"Failed to fetch {url}: {e}")
        return

    soup = BeautifulSoup(res.text, "html.parser")
    
    # 1. Target the <article> container
    article = soup.find("article") or soup.find("main") or soup.find("body")
    if not article:
        print(f"No article tag found for {url}")
        return

    # Extract title for the filename
    title = soup.find("h1")
    title_text = title.text.strip() if title else urlparse(url).path.strip("/").replace("/", "_") or "post"
    safe_title = sanitize_filename(title_text)

    # 2. Find and download all images in the <article>
    for img in article.find_all("img"):
        src = img.get("data-src") or img.get("src")
        if not src:
            continue

        full_img_url = urljoin(url, src)
        
        # Pass article prefix to image downloader
        local_rel_path = download_image(full_img_url, IMAGES_DIR, prefix_str=prefix)
        img["src"] = local_rel_path
        
        if "srcset" in img.attrs:
            del img["srcset"]

    # 3. Convert article HTML to Markdown
    md_content = markdownify.markdownify(
        str(article), 
        heading_style="ATX",
        code_language=""
    )

    # 4. Save to .md file with 00_, 01_ prefix
    md_filename = os.path.join(OUTPUT_DIR, f"{prefix}{safe_title}.md")
    with open(md_filename, "w", encoding="utf-8") as f:
        f.write(f"# {title_text}\n\n")
        f.write(md_content.strip())
        
    print(f" Successfully saved: {md_filename}\n")

if __name__ == "__main__":
    for idx, link in enumerate(URLS):
        process_url(idx, link)