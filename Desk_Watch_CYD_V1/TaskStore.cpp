#include "TaskStore.h"
#include <stdio.h>
#include <string.h>

namespace {
// Code points the bundled PostItFont can draw (see tools/make_postit_font.py).
bool inFont(uint32_t c) {
  return (c >= 0x20 && c <= 0x7E) || (c >= 0xA0 && c <= 0x17F) ||
         c == 0x2013 || c == 0x2014 || c == 0x2018 || c == 0x2019 || c == 0x201C ||
         c == 0x201D || c == 0x2022 || c == 0x2026 || c == 0x20AC;
}
// Decodes one UTF-8 sequence. Returns its length, or 0 if invalid.
int decode(const unsigned char* s, uint32_t& cp) {
  if (s[0] < 0x80) { cp = s[0]; return 1; }
  int n; uint32_t min;
  if ((s[0] & 0xE0) == 0xC0) { n = 2; cp = s[0] & 0x1F; min = 0x80; }
  else if ((s[0] & 0xF0) == 0xE0) { n = 3; cp = s[0] & 0x0F; min = 0x800; }
  else if ((s[0] & 0xF8) == 0xF0) { n = 4; cp = s[0] & 0x07; min = 0x10000; }
  else return 0;
  for (int i = 1; i < n; ++i) {
    if ((s[i] & 0xC0) != 0x80) return 0;
    cp = cp << 6 | (s[i] & 0x3F);
  }
  if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
  return n;
}
int encode(uint32_t c, char* out) {
  if (c < 0x80) { out[0] = char(c); return 1; }
  if (c < 0x800) { out[0] = char(0xC0 | c >> 6); out[1] = char(0x80 | (c & 0x3F)); return 2; }
  out[0] = char(0xE0 | c >> 12); out[1] = char(0x80 | ((c >> 6) & 0x3F)); out[2] = char(0x80 | (c & 0x3F));
  return 3;
}
uint8_t checksum(const uint8_t* data, size_t length) {
  uint8_t sum = 0x5A;
  for (size_t i = 0; i < length; ++i) sum = uint8_t((sum << 1 | sum >> 7) ^ data[i]);
  return sum;
}
}  // namespace

size_t sanitizeTaskText(const char* in, char* out, size_t capacity) {
  if (!capacity) return 0;
  size_t length = 0; bool pendingSpace = false;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(in ? in : "");
  while (*p) {
    uint32_t cp; int n = decode(p, cp);
    if (!n) { ++p; continue; }                       // drop invalid bytes
    p += n;
    if (cp <= 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0) || cp == 0xA0 || cp == 0x2028 || cp == 0x2029) {
      pendingSpace = true; continue;                 // whitespace / control
    }
    if (!inFont(cp)) cp = '?';
    char encoded[3]; const int size = encode(cp, encoded);
    const size_t need = size + (pendingSpace && length ? 1 : 0);
    if (length + need >= capacity) break;            // keep room for '\0', never split a character
    if (pendingSpace && length) out[length++] = ' ';
    pendingSpace = false;
    memcpy(out + length, encoded, size); length += size;
  }
  out[length] = '\0';
  return length;
}

size_t utf8DropLast(const char* s, size_t length) {
  if (!length) return 0;
  size_t i = length - 1;
  while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
  return i;
}

int taskRowAt(int x, int y) {
  if (x < 6 || x > 314 || y < TASK_TOP || y >= TASK_TOP + TASK_VISIBLE * TASK_ROW_H) return -1;
  return (y - TASK_TOP) / TASK_ROW_H;
}
int taskScrollAt(int x, int y) {
  if (y < 0 || y > 24) return 0;
  if (x >= 158 && x <= 194) return -1;
  if (x >= 214 && x <= 252) return 1;
  return 0;
}
int taskPageCount(int count) { return count <= 0 ? 1 : (count + TASK_VISIBLE - 1) / TASK_VISIBLE; }
int taskPageOf(int scroll) { return (scroll + TASK_VISIBLE - 1) / TASK_VISIBLE; }
int taskPageScroll(int scroll, int count, int direction) {
  int page = taskPageOf(clampTaskScroll(scroll, count)) + direction;
  if (page < 0) page = 0;
  if (page >= taskPageCount(count)) page = taskPageCount(count) - 1;
  return clampTaskScroll(page * TASK_VISIBLE, count);
}
int clampTaskScroll(int scroll, int count) {
  const int last = count > TASK_VISIBLE ? count - TASK_VISIBLE : 0;
  return scroll < 0 ? 0 : scroll > last ? last : scroll;
}

int TaskStore::indexOf(uint16_t id) const {
  for (int i = 0; i < count_; ++i) if (tasks_[i].id == id) return i;
  return -1;
}
void TaskStore::removeAt(int index) {
  for (int i = index; i + 1 < count_; ++i) tasks_[i] = tasks_[i + 1];
  tasks_[--count_] = TodoItem();
}
int TaskStore::doneCount() const {
  int n = 0; for (int i = 0; i < count_; ++i) n += tasks_[i].done; return n;
}
int TaskStore::firstDone() const {
  for (int i = 0; i < count_; ++i) if (tasks_[i].done) return i;
  return count_;
}
// Keeps the order "open tasks first, finished tasks last", otherwise preserving the existing order.
void TaskStore::stabilize() {
  TodoItem sorted[TASK_MAX]; int n = 0;
  for (int i = 0; i < count_; ++i) if (!tasks_[i].done) sorted[n++] = tasks_[i];
  for (int i = 0; i < count_; ++i) if (tasks_[i].done) sorted[n++] = tasks_[i];
  for (int i = 0; i < count_; ++i) tasks_[i] = sorted[i];
}
uint16_t TaskStore::add(const char* rawText) {
  if (count_ >= TASK_MAX) return 0;
  TodoItem task;
  if (!sanitizeTaskText(rawText, task.text, sizeof(task.text))) return 0;
  task.id = nextId_;
  if (++nextId_ == 0) nextId_ = 1;
  const int at = firstDone();                      // a new task goes after the open ones, above the finished ones
  for (int i = count_; i > at; --i) tasks_[i] = tasks_[i - 1];
  tasks_[at] = task; ++count_;
  return task.id;
}
bool TaskStore::toggle(uint16_t id) { const int i = indexOf(id); return i >= 0 && toggleAt(i); }
// Ticking a task sends it to the bottom of the list; unticking brings it back below the open tasks.
bool TaskStore::toggleAt(int index) {
  if (index < 0 || index >= count_) return false;
  TodoItem item = tasks_[index];
  item.done = !item.done;
  for (int i = index; i + 1 < count_; ++i) tasks_[i] = tasks_[i + 1];
  --count_;
  const int at = item.done ? count_ : firstDone();
  for (int i = count_; i > at; --i) tasks_[i] = tasks_[i - 1];
  tasks_[at] = item; ++count_;
  return true;
}
bool TaskStore::remove(uint16_t id) {
  const int i = indexOf(id); if (i < 0) return false; removeAt(i); return true;
}
int TaskStore::clearDone() {
  int removed = 0;
  for (int i = count_ - 1; i >= 0; --i) if (tasks_[i].done) { removeAt(i); ++removed; }
  return removed;
}

size_t TaskStore::serialize(uint8_t* out, size_t capacity) const {
  size_t need = 7;
  for (int i = 0; i < count_; ++i) need += 4 + strlen(tasks_[i].text);
  if (capacity < need) return 0;
  size_t n = 0;
  out[n++] = 'T'; out[n++] = 'K'; out[n++] = 1; out[n++] = uint8_t(count_);
  out[n++] = uint8_t(nextId_ & 0xFF); out[n++] = uint8_t(nextId_ >> 8);
  for (int i = 0; i < count_; ++i) {
    const size_t len = strlen(tasks_[i].text);
    out[n++] = uint8_t(tasks_[i].id & 0xFF); out[n++] = uint8_t(tasks_[i].id >> 8);
    out[n++] = tasks_[i].done ? 1 : 0; out[n++] = uint8_t(len);
    memcpy(out + n, tasks_[i].text, len); n += len;
  }
  out[n] = checksum(out, n); return n + 1;
}

bool TaskStore::deserialize(const uint8_t* in, size_t length) {
  if (length < 7 || in[0] != 'T' || in[1] != 'K' || in[2] != 1) return false;
  if (checksum(in, length - 1) != in[length - 1]) return false;
  const int count = in[3];
  if (count > TASK_MAX) return false;
  TaskStore fresh; fresh.nextId_ = uint16_t(in[4] | in[5] << 8);
  if (!fresh.nextId_) return false;
  size_t n = 6;
  for (int i = 0; i < count; ++i) {
    if (n + 4 > length - 1) return false;
    TodoItem t; t.id = uint16_t(in[n] | in[n + 1] << 8); t.done = in[n + 2] == 1;
    const size_t len = in[n + 3]; n += 4;
    if (!t.id || len == 0 || len >= TASK_TEXT_BYTES || n + len > length - 1) return false;
    if (fresh.indexOf(t.id) >= 0) return false;
    memcpy(t.text, in + n, len); t.text[len] = '\0'; n += len;
    char clean[TASK_TEXT_BYTES];
    if (sanitizeTaskText(t.text, clean, sizeof(clean)) != len || memcmp(clean, t.text, len)) return false;
    fresh.tasks_[fresh.count_++] = t;
  }
  if (n != length - 1) return false;
  fresh.stabilize();                               // lists saved by older versions may be unsorted
  *this = fresh; return true;
}

size_t TaskStore::toJson(char* out, size_t capacity) const {
  size_t n = 0;
  auto put = [&](const char* s) { const size_t l = strlen(s); if (n + l + 1 > capacity) return false; memcpy(out + n, s, l); n += l; out[n] = '\0'; return true; };
  char head[48];
  snprintf(head, sizeof head, "{\"max\":%d,\"tasks\":[", TASK_MAX);
  if (!put(head)) return 0;
  for (int i = 0; i < count_; ++i) {
    char item[40];
    snprintf(item, sizeof item, "%s{\"id\":%u,\"done\":%s,\"text\":\"", i ? "," : "", tasks_[i].id, tasks_[i].done ? "true" : "false");
    if (!put(item)) return 0;
    for (const char* c = tasks_[i].text; *c; ++c) {
      char piece[3] = {*c, 0, 0};
      if (*c == '"' || *c == '\\') { piece[0] = '\\'; piece[1] = *c; }
      if (!put(piece)) return 0;
    }
    if (!put("\"}")) return 0;
  }
  return put("]}") ? n : 0;
}
