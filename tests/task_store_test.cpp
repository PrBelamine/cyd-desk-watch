#include "TaskStore.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void testSanitize() {
  char o[TASK_TEXT_BYTES];
  assert(sanitizeTaskText("  Acheter  du\tpain\n ", o, sizeof o) == 15 && !strcmp(o, "Acheter du pain"));
  assert(sanitizeTaskText("   \n\t ", o, sizeof o) == 0 && !*o);
  assert(sanitizeTaskText(nullptr, o, sizeof o) == 0);
  assert(!strcmp((sanitizeTaskText("Réunion à 14h \xE2\x80\x94 \xC5\x93uf", o, sizeof o), o), "Réunion à 14h \xE2\x80\x94 \xC5\x93uf"));
  // invalid UTF-8 (lone continuation, truncated sequence, overlong) is dropped
  assert(!strcmp((sanitizeTaskText("a\x80" "b\xC3" "c\xC0\xAF" "d", o, sizeof o), o), "abcd"));
  // surrogate, emoji and CJK become '?', C1 control is whitespace
  assert(!strcmp((sanitizeTaskText("x\xED\xA0\x80y \xF0\x9F\x98\x80 \xE4\xB8\xAD", o, sizeof o), o), "xy ? ?"));
  assert(!strcmp((sanitizeTaskText("a\xC2\x85" "b", o, sizeof o), o), "a b"));
  // truncation never splits a character: 59 ASCII + 'é' (2 bytes) does not fit in 60
  char longText[100]; memset(longText, 'a', 59); strcpy(longText + 59, "é");
  assert(sanitizeTaskText(longText, o, sizeof o) == 59 && o[59] == 0);
  memset(longText, 'a', 58); strcpy(longText + 58, "é");
  assert(sanitizeTaskText(longText, o, sizeof o) == 60);
  char tiny[4];
  assert(sanitizeTaskText("abcdef", tiny, sizeof tiny) == 3 && !strcmp(tiny, "abc"));
  assert(utf8DropLast("aé", 3) == 1 && utf8DropLast("ab", 2) == 1 && utf8DropLast("", 0) == 0);
}
static void testStore() {
  TaskStore s;
  const uint16_t a = s.add("Lait"), b = s.add("Pain"), c = s.add("Œufs");
  assert(a == 1 && b == 2 && c == 3 && s.count() == 3);
  assert(s.add("   ") == 0 && s.count() == 3);
  assert(s.toggle(b) && s.at(2).done && !strcmp(s.at(2).text, "Pain") && s.doneCount() == 1);
  assert(!s.toggle(99) && !s.toggleAt(3) && !s.toggleAt(-1));
  assert(s.toggleAt(0) && s.doneCount() == 2);
  assert(s.clearDone() == 2 && s.count() == 1 && !strcmp(s.at(0).text, "Œufs"));
  assert(s.remove(c) && s.count() == 0 && !s.remove(c));
  assert(s.add("Neuf") == 4);                       // ids are never reused
  for (int i = 1; i < TASK_MAX; ++i) assert(s.add("x"));
  assert(s.count() == TASK_MAX && s.add("trop") == 0);
}
static void order(const TaskStore& s, char* out) {          // ids in list order, e.g. "ab" ; uppercase = finished
  for (int i = 0; i < s.count(); ++i) { const char c = char('a' + s.at(i).text[0] - 'a'); out[i] = s.at(i).done ? char(c - 32) : c; }
  out[s.count()] = 0;
}
static void testOrdering() {
  TaskStore s; char o[TASK_MAX + 1];
  s.add("a"); s.add("b"); s.add("c"); s.add("d");
  s.toggleAt(1); order(s, o); assert(!strcmp(o, "acdB"));          // ticked task goes to the bottom
  s.toggleAt(0); order(s, o); assert(!strcmp(o, "cdBA"));          // the next one goes below it
  s.add("e"); order(s, o); assert(!strcmp(o, "cdeBA"));            // new tasks stay above the finished ones
  s.toggleAt(3); order(s, o); assert(!strcmp(o, "cdebA"));         // unticking: back below the open tasks
  s.toggleAt(0); s.toggleAt(0); order(s, o); assert(!strcmp(o, "ebACD"));
  for (int i = 0; i + 1 < s.count(); ++i) assert(!(s.at(i).done && !s.at(i + 1).done));   // never a finished task above an open one
  assert(s.doneCount() == 3 && s.count() == 5);
  TaskStore t; t.add("x"); t.add("y"); t.toggleAt(0); t.toggleAt(0); order(t, o); assert(!strcmp(o, "XY"));
  uint8_t buf[TASK_STORAGE_MAX]; const size_t n = s.serialize(buf, sizeof buf);
  TaskStore r; assert(r.deserialize(buf, n)); char o2[TASK_MAX + 1]; order(s, o); order(r, o2); assert(!strcmp(o, o2));
  assert(s.toggle(s.at(0).id) && s.at(s.count() - 1).done);        // toggling by id behaves the same way
}
static void testPersistence() {
  TaskStore s; s.add("Appeler \"Marie\" \\ ok"); s.add("Réunion à 14h"); s.toggleAt(1);
  uint8_t buf[TASK_STORAGE_MAX];
  const size_t n = s.serialize(buf, sizeof buf);
  assert(n > 0 && n <= TASK_STORAGE_MAX);
  assert(s.serialize(buf, n - 1) == 0);
  TaskStore r; assert(r.deserialize(buf, n));
  assert(r.count() == 2 && r.at(1).done && !r.at(0).done && !strcmp(r.at(1).text, "Réunion à 14h") && r.at(1).id == 2);
  assert(r.add("suite") == 3);                      // nextId survives a reboot
  // any corruption is rejected and leaves the store unchanged
  for (size_t i = 0; i < n; ++i) {
    uint8_t bad[TASK_STORAGE_MAX]; memcpy(bad, buf, n); bad[i] ^= 0x01;
    TaskStore t; t.add("garde"); assert(!t.deserialize(bad, n) && t.count() == 1);
  }
  TaskStore t; assert(!t.deserialize(buf, n - 1) && !t.deserialize(buf, 0));
  // a full list fits the advertised storage size
  TaskStore full; char word[TASK_TEXT_BYTES]; memset(word, 'z', 60); word[60] = 0;
  for (int i = 0; i < TASK_MAX; ++i) full.add(word);
  uint8_t big[TASK_STORAGE_MAX]; assert(full.serialize(big, sizeof big) == TASK_STORAGE_MAX);
}
static void testJson() {
  TaskStore s; char j[TASK_JSON_MAX];
  assert(s.toJson(j, sizeof j) && !strcmp(j, "{\"max\":18,\"tasks\":[]}"));
  s.add("A \"b\" \\c"); s.toggleAt(0); s.add("é");
  const size_t n = s.toJson(j, sizeof j);
  assert(n == strlen(j));
  assert(!strcmp(j, "{\"max\":18,\"tasks\":[{\"id\":2,\"done\":false,\"text\":\"é\"},{\"id\":1,\"done\":true,\"text\":\"A \\\"b\\\" \\\\c\"}]}"));
  assert(s.toJson(j, 10) == 0);
  // worst case (18 tasks of 60 escaped characters) still fits TASK_JSON_MAX
  TaskStore full; char q[61]; memset(q, '"', 60); q[60] = 0;
  for (int i = 0; i < TASK_MAX; ++i) full.add(q);
  assert(full.toJson(j, sizeof j) > 0);
}
static void testTouch() {
  assert(taskRowAt(100, 30) == -1 && taskRowAt(100, 31) == 0 && taskRowAt(100, 61) == 0 && taskRowAt(100, 62) == 1);
  assert(taskRowAt(100, 216) == 5 && taskRowAt(100, 217) == -1 && taskRowAt(5, 100) == -1 && taskRowAt(315, 100) == -1);
  assert(taskScrollAt(176, 10) == -1 && taskScrollAt(232, 10) == 1 && taskScrollAt(290, 10) == 0 && taskScrollAt(150, 10) == 0 && taskScrollAt(300, 40) == 0);
  assert(taskPageCount(0) == 1 && taskPageCount(6) == 1 && taskPageCount(7) == 2 && taskPageCount(18) == 3);
  assert(taskPageScroll(0, 18, 1) == 6 && taskPageScroll(6, 18, 1) == 12 && taskPageScroll(12, 18, 1) == 12);     // page 2, then page 3, then stays
  assert(taskPageScroll(12, 18, -1) == 6 && taskPageScroll(6, 18, -1) == 0 && taskPageScroll(0, 18, -1) == 0);
  assert(taskPageScroll(0, 14, 1) == 6 && taskPageScroll(6, 14, 1) == 8 && taskPageScroll(8, 14, -1) == 6);      // last page is aligned to the end of the list
  assert(taskPageOf(0) == 0 && taskPageOf(6) == 1 && taskPageOf(8) == 2);
  assert(clampTaskScroll(-3, 10) == 0 && clampTaskScroll(3, 10) == 3 && clampTaskScroll(9, 10) == 4 && clampTaskScroll(5, 4) == 0);
}
int main() { testSanitize(); testStore(); testOrdering(); testPersistence(); testJson(); testTouch(); puts("task_store_test ok"); }
