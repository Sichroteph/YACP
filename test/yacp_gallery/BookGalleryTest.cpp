#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "BookGallery.h"
#include "HalStorage.h"

int main() {
  char folder[BookGallery::FOLDER_CAPACITY];
  char selected[BookGallery::IMAGE_PATH_CAPACITY];
  assert(!BookGallery::folderForBook("/photo.png",folder));
  assert(BookGallery::folderForBook("/completion.epub",folder));
  const std::string root=folder;
  assert(root == "/.book-galleries/0795821da95c0fb8-completion");
  assert(!BookGallery::selectSleepImage("/completion.epub",selected));
  // In-memory mock models directory existence through child file paths.
  testFiles[root+"/one.bmp"]={'B','M',1};
  testFiles[root+"/two.BMP"]={'B','M',2};
  testFiles[root+"/invalid.bmp"]={'n','o'};
  testFiles[root+"/picture.png"]={'B','M'};
  testFiles[root+"/.hidden.bmp"]={'B','M'};
  // Explicit directory record is unnecessary for the fake filesystem. exists
  // is patched below to model directories as the real HAL does.
  std::string previous;
  for (unsigned i=0;i<100;++i) {
    assert(BookGallery::selectSleepImage("/completion.epub",selected));
    assert(std::string(selected)==root+"/one.bmp" || std::string(selected)==root+"/two.BMP");
    assert(previous!=selected);
    assert(BookGallery::rememberSleepImage(selected));
    previous=selected;
  }
  const auto snapshot=testFiles;
  assert(BookGallery::folderForBook("/other.epub",folder) && folder!=root);
  assert(!BookGallery::selectSleepImage("/other.epub",selected));
  assert(testFiles==snapshot);
  testFiles.erase(root+"/two.BMP");
  for (unsigned i=0;i<3;++i) {
    assert(BookGallery::selectSleepImage("/completion.epub",selected));
    assert(std::string(selected)==root+"/one.bmp");
    assert(BookGallery::rememberSleepImage(selected));
  }
  const auto marker=root+"/.last-sleep-bmp";
  testFiles[marker]={'n','o','n','e','.','b','m','p','\r','\n'};
  assert(BookGallery::selectSleepImage("/completion.epub",selected));
  failStatsWrite=true; assert(!BookGallery::rememberSleepImage(selected)); failStatsWrite=false;
  failStatsClose=true; assert(!BookGallery::rememberSleepImage(selected)); failStatsClose=false;
  testFiles.erase(root+"/one.bmp");
  assert(!BookGallery::selectSleepImage("/completion.epub",selected));
  assert(selected[0]==0);
  assert(!BookGallery::rememberSleepImage("/other/place.bmp"));
  std::puts("Book galleries: stable identity, non-repeat, independent books, fallback, bounded selection and marker errors passed");
}
