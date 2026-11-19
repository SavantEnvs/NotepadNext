// Fuzz NotepadNext's Edit > Line Operations / Comment / Sort commands on a loaded document:
// ScintillaNext::removeDuplicateLines, removeConsecutiveDuplicateLines, trimTrailingWhitespace,
// deleteLeadingEmptyLines, deleteTrailingEmptyLines, toggle/comment/uncommentLineSelection
// (ScintillaCommenter) and ScintillaSorter with each Sorter (src/Sorter.cpp). They split the raw
// document bytes into lines (ByteArrayUtils), rebuild the text and replace the target range.
//
// Input layout:  [0] first op  [1] selection start  [2] selection end  [3] second op
//                [4] comment-marker choice  [5..] document text (raw bytes, mixed EOLs)
// The selection bounds are fractions (n/255) of the document length, as a user selection would be.
// After both ops the undo history is replayed to the start (what Ctrl+Z does).
// State: one offscreen QApplication, created once; each input gets its own editor.

#include <cstdint>

#include "ScintillaNext.h"
#include "ScintillaSorter.h"
#include "Sorter.h"
#include "qt_fuzz_common.h"

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    nnFuzzInitQt();
    return 0;
}

static void runOp(ScintillaNext &editor, uint8_t op) {
    const auto dir = (op & 0x80) ? Sorter::Direction::Descending : Sorter::Direction::Ascending;
    switch ((op & 0x7f) % 12) {
    case 0: editor.removeDuplicateLines(); break;
    case 1: editor.removeConsecutiveDuplicateLines(); break;
    case 2: editor.trimTrailingWhitespace(); break;
    case 3: editor.deleteLeadingEmptyLines(); break;
    case 4: editor.deleteTrailingEmptyLines(); break;
    case 5: editor.toggleCommentSelection(); break;
    case 6: editor.commentLineSelection(); break;
    case 7: editor.uncommentLineSelection(); break;
    case 8: ScintillaSorter(&editor).sort(CaseSensitiveSorter(dir)); break;
    case 9: ScintillaSorter(&editor).sort(CaseInsensitiveSorter(dir)); break;
    case 10: ScintillaSorter(&editor).sort(LineLengthSorter(dir)); break;
    case 11: ScintillaSorter(&editor).sort(ReverseSorter(dir)); break;
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 5)
        return -1;
    static const char *const kComments[4] = {"//", "#", "--", "; "};

    ScintillaNext editor(QStringLiteral("fuzz"));
    editor.setCodePage(SC_CP_UTF8);
    editor.languageSingleLineComment = kComments[data[4] & 3];
    editor.appendText(static_cast<sptr_t>(size - 5), reinterpret_cast<const char *>(data + 5));
    editor.emptyUndoBuffer();

    const sptr_t length = editor.length();
    editor.setSelection(length * data[2] / 255, length * data[1] / 255);  // caret, anchor
    runOp(editor, data[0]);

    const sptr_t length2 = editor.length();
    editor.setSelection(length2 * data[1] / 255, length2 * data[2] / 255);
    runOp(editor, data[3]);

    for (int i = 0; i < 64 && editor.canUndo(); i++)
        editor.undo();
    volatile sptr_t sink = editor.length();
    (void)sink;
    return 0;
}
