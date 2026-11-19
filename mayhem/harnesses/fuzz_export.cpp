// Fuzz NotepadNext's "Copy as / Export as HTML and RTF": HtmlConverter and RtfConverter
// (src/HtmlConverter.cpp, src/RtfConverter.cpp) walk the styled document byte by byte, escape it
// and emit the style table.
//
// Input layout:  [0] styling mode   [1] sub-range selector   [2..] payload
//   [0] bit 7 clear: the payload is the text, styled by a lexer from kLexers[[0] % size], as in the
//                    editor. The list is a few common, well-exercised lexers on purpose: every Lexilla
//                    lexer is fuzzed by fuzz_lexilla, and a lexer fault would abort the input before
//                    the converters run, so this target spends its time on HtmlConverter/RtfConverter.
//   [0] bit 7 set:   no lexer; the payload is (text byte, style byte) pairs and the styles are applied
//                    directly (SCI_STARTSTYLING + SCI_SETSTYLINGEX), reaching every style number and
//                    style-change pattern the converters' escaping and style-table code can see.
//                    Style bytes are masked to 0..127: that is the range Lexilla lexers emit when no
//                    substyles are allocated, and NotepadNext never allocates any. (Styles >= 128 come
//                    back negative from SCI_GETSTYLEAT, which returns a signed char.)
// The whole document is converted to HTML and RTF, then a sub-range (start/end derived from byte 1)
// is converted again, like exporting a selection.
// State: one offscreen QApplication, created once; each input gets its own editor and lexer
// (Scintilla owns the lexer and releases it with the document).

#include <cstdint>

#include <QByteArray>
#include <QTextStream>

#include "ILexer.h"
#include "HtmlConverter.h"
#include "Lexilla.h"
#include "RtfConverter.h"
#include "ScintillaNext.h"
#include "qt_fuzz_common.h"

namespace {
const char *const kLexers[] = {
    "cpp", "python", "hypertext", "xml", "lua", "json", "bash", "sql",
    "markdown", "yaml", "css", "props", "makefile", "batch", "diff", "null",
};
}  // namespace

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    nnFuzzInitQt();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2)
        return -1;
    ScintillaNext editor(QStringLiteral("fuzz"));
    editor.setCodePage(SC_CP_UTF8);
    const char *text = reinterpret_cast<const char *>(data + 2);
    const size_t payload = size - 2;
    if (data[0] & 0x80) {
        // Container styling: no lexer, so SCI_COLOURISE in the converters leaves these styles alone.
        const size_t n = payload / 2;
        QByteArray chars(static_cast<qsizetype>(n), '\0');
        QByteArray styles(static_cast<qsizetype>(n), '\0');
        for (size_t i = 0; i < n; i++) {
            chars[static_cast<qsizetype>(i)] = text[2 * i];
            styles[static_cast<qsizetype>(i)] = static_cast<char>(text[2 * i + 1] & 0x7f);
        }
        editor.appendText(static_cast<sptr_t>(n), chars.constData());
        editor.startStyling(0, 0);
        editor.setStylingEx(static_cast<sptr_t>(n), styles.constData());
    } else {
        constexpr size_t kCount = sizeof(kLexers) / sizeof(kLexers[0]);
        Scintilla::ILexer5 *lexer = CreateLexer(kLexers[data[0] % kCount]);
        if (lexer)
            editor.setILexer(reinterpret_cast<sptr_t>(lexer));
        editor.appendText(static_cast<sptr_t>(payload), text);
    }

    QByteArray out;
    {
        QTextStream stream(&out);
        HtmlConverter html(&editor);
        html.convert(stream);
        RtfConverter rtf(&editor);
        rtf.convert(stream);

        const int length = static_cast<int>(editor.length());
        const int start = length ? (data[1] & 0x0f) * length / 16 : 0;
        const int end = start + ((data[1] >> 4) + 1) * (length - start) / 16;
        html.convertRange(stream, start, end);
        rtf.convertRange(stream, start, end);
        stream.flush();
    }
    volatile qsizetype sink = out.size();
    (void)sink;
    return 0;
}
