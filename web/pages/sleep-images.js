const SLEEP_IMAGES_DIR = "/.sleep";
let currentBookPath = "";
let currentBookName = "";
let selectedBookPath = "";
let selectedGalleryPath = "";

function escapeHtml(unsafe) {
  return String(unsafe || "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function bookNameFromPath(path) {
  return (path || "").split("/").filter(Boolean).pop() || path || "";
}

function formatFileSize(bytes) {
  if (!bytes) return "0 B";
  const k = 1024;
  const sizes = ["B", "KB", "MB", "GB"];
  const index = Math.min(Math.floor(Math.log(bytes) / Math.log(k)), sizes.length - 1);
  return parseFloat((bytes / Math.pow(k, index)).toFixed(2)).toLocaleString() + " " + sizes[index];
}

function downloadUrl(path) {
  return `/download?path=${encodeURIComponent(path)}`;
}

function pathInFolder(folderPath, name) {
  return (folderPath.endsWith("/") ? folderPath : folderPath + "/") + name;
}

function isImageFile(name) {
  return /\.(png|jpe?g|bmp|gif|webp)$/i.test(name);
}

async function listFilesForPath(path) {
  const response = await fetch("/api/files?path=" + encodeURIComponent(path) + "&_=" + Date.now());
  if (!response.ok) throw new Error("Failed to load images");
  return await response.json();
}

function imageEntriesFromFiles(path, files) {
  return files
    .filter((file) => file && !file.isDirectory && isImageFile(file.name))
    .sort((a, b) => a.name.localeCompare(b.name))
    .map((file) => ({
      name: file.name,
      path: pathInFolder(path, file.name),
      size: file.size || 0,
    }));
}

function imageUsageLabel(name) {
  const lowerName = name.toLowerCase();
  if (lowerName.endsWith(".bmp")) return "Ready for sleep screen";
  if (lowerName.endsWith(".png")) return "Available in overlay mode";
  return "Original image - prepare it to use during sleep";
}

function renderImageList(containerId, images, emptyText) {
  const container = document.getElementById(containerId);
  if (!container) return;
  if (!images || images.length === 0) {
    container.innerHTML = `<div class="empty-state">${escapeHtml(emptyText)}</div>`;
    return;
  }

  container.innerHTML = images
    .map((image) => {
      const encodedPath = encodeURIComponent(image.path);
      const encodedName = encodeURIComponent(image.name);
      return `<div class="item-row">
        <div class="item-main">
          <div class="item-name" title="${escapeHtml(image.name)}">${escapeHtml(image.name)}</div>
          <div class="item-meta">${escapeHtml(imageUsageLabel(image.name))} &middot; ${formatFileSize(image.size)}</div>
        </div>
        <div class="row-actions">
          <button type="button" data-path="${encodedPath}" data-name="${encodedName}" onclick="previewImage(this)">View</button>
          <button type="button" class="danger" data-path="${encodedPath}" data-name="${encodedName}" onclick="deleteImage(this)">Delete</button>
        </div>
      </div>`;
    })
    .join("");
}

async function loadGlobalImages() {
  const list = document.getElementById("globalImageList");
  list.innerHTML = '<div class="empty-state">Loading...</div>';
  try {
    const prepareResponse = await fetch("/api/sleep-images/prepare", { method: "POST" });
    if (!prepareResponse.ok) throw new Error("Unable to prepare image folder");
    const files = await listFilesForPath(SLEEP_IMAGES_DIR);
    renderImageList("globalImageList", imageEntriesFromFiles(SLEEP_IMAGES_DIR, files), "No default images yet.");
  } catch (error) {
    list.innerHTML = '<div class="empty-state">Unable to load default images.</div>';
  }
}

async function loadBooks() {
  const select = document.getElementById("bookSelect");
  select.innerHTML = '<option value="">Loading books...</option>';

  try {
    const [statusResponse, booksResponse] = await Promise.all([
      fetch("/api/status?_=" + Date.now()),
      fetch("/api/books?_=" + Date.now()),
    ]);

    if (statusResponse.ok) {
      const status = await statusResponse.json();
      currentBookPath = status.currentBookPath || "";
      currentBookName = status.currentBookName || bookNameFromPath(currentBookPath);
    }

    const booksByPath = new Map();
    if (currentBookPath) booksByPath.set(currentBookPath, currentBookName);
    if (booksResponse.ok) {
      const found = await booksResponse.json();
      found.forEach((book) => {
        if (book && book.path) booksByPath.set(book.path, bookNameFromPath(book.path));
      });
    }

    const books = Array.from(booksByPath, ([path, name]) => ({ path, name })).sort((a, b) =>
      a.name.localeCompare(b.name),
    );
    select.innerHTML = '<option value="">Select a book...</option>';
    books.forEach((book) => {
      const option = document.createElement("option");
      option.value = book.path;
      option.textContent = book.path === currentBookPath ? `Currently reading: ${book.name}` : book.name;
      select.appendChild(option);
    });

    if (currentBookPath) {
      select.value = currentBookPath;
      await loadSelectedBookGallery(currentBookPath);
    } else if (books.length === 0) {
      select.innerHTML = '<option value="">No books found</option>';
    }
  } catch (error) {
    select.innerHTML = '<option value="">Unable to load books</option>';
  }
}

async function loadSelectedBookGallery(bookPath) {
  selectedBookPath = bookPath || "";
  selectedGalleryPath = "";
  document.getElementById("addBookImagesBtn").disabled = !selectedBookPath;
  const list = document.getElementById("selectedGalleryList");

  if (!selectedBookPath) {
    list.innerHTML = '<div class="empty-state">Select a book to see its images.</div>';
    return;
  }

  list.innerHTML = '<div class="empty-state">Loading...</div>';
  try {
    const formData = new FormData();
    formData.append("bookPath", selectedBookPath);
    const response = await fetch("/api/book-gallery/prepare", { method: "POST", body: formData });
    if (!response.ok) throw new Error(await response.text());
    const data = await response.json();
    selectedGalleryPath = data.path || "";
    const files = await listFilesForPath(selectedGalleryPath);
    renderImageList("selectedGalleryList", imageEntriesFromFiles(selectedGalleryPath, files), "No images for this book yet.");
  } catch (error) {
    list.innerHTML = '<div class="empty-state">Unable to load images for this book.</div>';
  }
}

function openPreparedUploader(destination, bookPath = "") {
  const params = new URLSearchParams({
    upload: destination,
    prepare: "1",
    return: "sleep-images",
  });
  if (bookPath) params.set("book", bookPath);
  window.location.href = "/files?" + params.toString();
}

function addGlobalImages() {
  openPreparedUploader("sleep-images");
}

function addBookImages() {
  if (selectedBookPath) openPreparedUploader("book-gallery", selectedBookPath);
}

function previewImage(button) {
  const path = decodeURIComponent(button.dataset.path || "");
  const name = decodeURIComponent(button.dataset.name || "");
  if (!path) return;
  document.getElementById("previewName").textContent = name || path;
  document.getElementById("previewImage").src = downloadUrl(path);
  document.getElementById("previewImage").alt = name || path;
  document.getElementById("previewDownload").href = downloadUrl(path);
  document.getElementById("previewModal").classList.add("open");
}

function closePreview() {
  document.getElementById("previewModal").classList.remove("open");
  document.getElementById("previewImage").src = "";
}

async function deleteImage(button) {
  const path = decodeURIComponent(button.dataset.path || "");
  const name = decodeURIComponent(button.dataset.name || bookNameFromPath(path));
  if (!path || !confirm(`Delete ${name}?`)) return;

  const response = await fetch("/delete", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: "path=" + encodeURIComponent(path),
  });
  if (!response.ok) {
    alert((await response.text()) || "Delete failed");
    return;
  }

  if (path.startsWith(SLEEP_IMAGES_DIR + "/")) {
    await loadGlobalImages();
  } else if (selectedBookPath) {
    await loadSelectedBookGallery(selectedBookPath);
  }
}

document.querySelectorAll(".modal-overlay").forEach((overlay) => {
  overlay.addEventListener("click", (event) => {
    if (event.target === overlay) closePreview();
  });
});

loadGlobalImages();
loadBooks();
