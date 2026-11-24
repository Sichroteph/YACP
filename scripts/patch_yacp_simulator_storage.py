"""Align a local simulator dependency with the physical SdFat directory contract.

Run before native migration acceptance. Does not change the SDK or SD data.
"""
import argparse
from pathlib import Path


def patch(path):
    text = path.read_text(encoding="utf-8")
    start = text.index("bool HalStorage::mkdir(")
    end = text.index("bool HalStorage::exists(", start)
    old = text[start:end]
    old_return = "return ::mkdir(full.c_str(), 0777) == 0 || errno == EEXIST;"
    if old_return in old:
        text = text[:start] + old.replace(old_return,
            "// SdFat rejects an already existing final component.\n  return ::mkdir(full.c_str(), 0777) == 0;") + text[end:]
    elif "return ::mkdir(full.c_str(), 0777) == 0;" not in old:
        raise ValueError("Unknown simulator mkdir implementation")
    old = "bool HalStorage::ensureDirectoryExists(const char *path) { return mkdir(path); }"
    new = """bool HalStorage::ensureDirectoryExists(const char *path) {
  // Same contract as SDCardManager: accept only an existing directory.
  if (exists(path)) {
    auto dir = open(path);
    const bool valid = dir && dir.isDirectory();
    dir.close();
    return valid;
  }
  return mkdir(path);
}"""
    if old in text:
        text = text.replace(old, new)
    elif new not in text:
        raise ValueError("Unknown simulator ensureDirectoryExists implementation")
    path.write_text(text, encoding="utf-8")
    print("Simulator directory contract aligned with SdFat:", path)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("hal_storage", type=Path)
    patch(parser.parse_args().hal_storage)
