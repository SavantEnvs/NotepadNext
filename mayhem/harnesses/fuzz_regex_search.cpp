// Fuzz NotepadNext's Find/Replace over a document: Finder (src/Finder.cpp) driving Scintilla's
// SCI_SEARCHINTARGET/SCI_FINDTEXT/SCI_REPLACETARGETRE, which with SCI_OWNREGEX (as upstream builds)
// run through the app's QRegexSearch (src/QRegexSearch.cpp): UTF-8 <-> UTF-16 position mapping
// over the Scintilla document, QRegularExpression matching, and \N / $N group substitution.
//
// Input layout:  [0] option bits   then  pattern '\0' replacement '\0' document text
//   bit0 plain text instead of regex, bit1 MatchCase, bit2 WholeWord, bit3 WordStart,
//   bit4 wrap-around, bit5 also run "replace selection if match" after a findNext.
// A missing separator leaves the replacement and/or the document empty. The pattern and the
// replacement are UTF-8, as the Find dialog passes them (QString -> toUtf8); the document is raw
// bytes, as a file loaded from disk.
// State: one offscreen QApplication, created once; each input gets its own editor and Finder.

#include <cstdint>
#include <cstring>

#include "Finder.h"
#include "ScintillaNext.h"
#include "qt_fuzz_common.h"

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    nnFuzzInitQt();
    return 0;
}

static const char *nextField(const char *p, const char *end, const char **fieldEnd) {
    const char *nul = static_cast<const char *>(memchr(p, '\0', end - p));
    *fieldEnd = nul ? nul : end;
    return nul ? nul + 1 : end;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2)
        return -1;
    const uint8_t bits = data[0];
    const char *p = reinterpret_cast<const char *>(data + 1);
    const char *end = reinterpret_cast<const char *>(data + size);
    const char *patEnd, *repEnd;
    const char *rep = nextField(p, end, &patEnd);
    const char *text = nextField(rep, end, &repEnd);

    ScintillaNext editor(QStringLiteral("fuzz"));
    editor.setCodePage(SC_CP_UTF8);
    editor.appendText(end - text, text);

    Finder finder(&editor);
    FindOptions &opt = finder.options();
    opt.text = QString::fromUtf8(p, patEnd - p);
    int flags = (bits & 1) ? 0 : static_cast<int>(Scintilla::FindOption::RegExp);
    if (bits & 2) flags |= static_cast<int>(Scintilla::FindOption::MatchCase);
    if (bits & 4) flags |= static_cast<int>(Scintilla::FindOption::WholeWord);
    if (bits & 8) flags |= static_cast<int>(Scintilla::FindOption::WordStart);
    opt.flags = static_cast<Scintilla::FindOption>(flags);
    opt.wrapAround = (bits & 16) != 0;
    const QString replacement = QString::fromUtf8(rep, repEnd - rep);

    volatile int sink = finder.count();
    Sci_Position from = 0;
    for (int i = 0; i < 32; i++) {
        FindResult r = finder.findNextFrom(from);
        if (!r)
            break;
        sink += r.range.cpMax - r.range.cpMin;
        if (r.range.cpMax <= from || r.wrapped)
            break;
        from = r.range.cpMax;
    }
    FindResult prev = finder.findPrev();
    if (prev && (bits & 32)) {
        editor.setSelection(prev.range.cpMin, prev.range.cpMax);
        finder.replaceSelectionIfMatch(replacement);
    }
    sink += finder.replaceAll(replacement);
    sink += static_cast<int>(editor.length());
    (void)sink;
    return 0;
}
