"""Preserve recent-book order and metadata without pruning unavailable books."""
from copy import deepcopy

FIELDS = frozenset(("path", "title", "author", "coverBmpPath"))


def candidate(document, target_capacity):
    if type(target_capacity) is not int or target_capacity <= 0:
        raise ValueError("Expected an explicit positive target recent-book capacity")
    if not isinstance(document, dict) or not isinstance(document.get("books"), list):
        raise ValueError("Expected the frozen YACP recent.json object")
    blocked = []
    for key in document.keys() - {"books"}:
        blocked.append(f"Unknown recent-document field: {key}")
    books = document["books"]
    if len(books) > target_capacity:
        blocked.append("Target loader would truncate the recent-book list")
    paths = set()
    for index, book in enumerate(books):
        if not isinstance(book, dict) or set(book) != FIELDS or not all(isinstance(v, str) for v in book.values()):
            blocked.append(f"Invalid or unreviewed recent-book fields at index {index}")
            continue
        path = book["path"]
        if (not path.startswith("/") or path.startswith("//") or "\\" in path
                or any(part in (".", "..") for part in path.split("/"))
                or any(ord(c) < 32 for c in path)):
            blocked.append(f"Unsafe book path at index {index}")
        if path in paths:
            blocked.append(f"Duplicate recent-book path at index {index}")
        paths.add(path)
    return {"recent": deepcopy(document), "blocked": sorted(blocked), "source_count": len(books),
            "target_capacity": target_capacity, "order_preserved": True,
            "book_existence_checked": False, "ready_to_activate": False,
            "cover_policy": "Keep original cover paths and their source files; regenerate only through the normal reader"}
