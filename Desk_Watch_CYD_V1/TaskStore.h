#pragma once
// Digital Post-it task list: pure C++ (no Arduino), so it is unit-tested on a PC.
#include <stddef.h>
#include <stdint.h>

constexpr int TASK_MAX = 18;                      // three screens of six
constexpr size_t TASK_TEXT_BYTES = 61;   // 60 UTF-8 bytes + terminator
constexpr size_t TASK_STORAGE_MAX = 6 + TASK_MAX * (4 + (TASK_TEXT_BYTES - 1)) + 1;
constexpr size_t TASK_JSON_MAX = 3300;

struct TodoItem {
  uint16_t id = 0;
  bool done = false;
  char text[TASK_TEXT_BYTES] = "";
};

// Cleans text typed on the web page: valid UTF-8 only, control characters and
// whitespace runs collapsed to one space, trimmed, characters missing from the
// on-screen font replaced by '?', cut on a character boundary. Returns the byte
// length written to out (0 means "nothing usable").
size_t sanitizeTaskText(const char* in, char* out, size_t capacity);
// Length of s once its last UTF-8 character is removed (for "..." truncation).
size_t utf8DropLast(const char* s, size_t length);

// Touch geometry of the Post-it screen, shared by the renderer and the touch handler.
constexpr int TASK_VISIBLE = 6, TASK_TOP = 31, TASK_ROW_H = 31;
int taskRowAt(int x, int y);                 // visible row 0..TASK_VISIBLE-1 under a tap, or -1
int taskScrollAt(int x, int y);              // -1 = up arrow, +1 = down arrow, 0 = neither
int clampTaskScroll(int scroll, int count);  // keeps the visible window inside the list
int taskPageCount(int count);                // number of screens (at least 1)
int taskPageOf(int scroll);                  // 0-based screen currently shown
int taskPageScroll(int scroll, int count, int direction);   // scroll offset after going one screen up (-1) or down (+1)

class TaskStore {
 public:
  int count() const { return count_; }
  int doneCount() const;
  const TodoItem& at(int index) const { return tasks_[index]; }
  uint16_t add(const char* rawText);          // returns the new id, 0 if refused
  bool toggle(uint16_t id);
  bool toggleAt(int index);                    // ticking moves the task to the bottom, unticking to just below the open tasks
  bool remove(uint16_t id);
  int clearDone();                              // returns how many were removed
  size_t serialize(uint8_t* out, size_t capacity) const;
  bool deserialize(const uint8_t* in, size_t length);  // leaves the store untouched on failure
  size_t toJson(char* out, size_t capacity) const;     // 0 if capacity is too small
 private:
  TodoItem tasks_[TASK_MAX];
  int count_ = 0;
  uint16_t nextId_ = 1;
  int indexOf(uint16_t id) const;
  void removeAt(int index);
  int firstDone() const;      // index of the first finished task (count() if none)
  void stabilize();           // open tasks first, finished tasks last
};
