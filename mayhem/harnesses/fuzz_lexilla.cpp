// Fuzz the vendored Lexilla lexers and folders (thirdparty/lexilla), which NotepadNext runs on every
// opened file (the lexer is picked from the file's extension or first line).
//
// Input layout:  [0] lexer index (mod GetLexerCount())   [1] property bits   [2..] document text
// The document is lexed and folded through Lexilla's public C API (CreateLexer -> ILexer5::Lex/Fold),
// the way Scintilla drives it, over Lexilla's own IDocument test implementation (test/TestDocument).
// TestDocument bounds-checks with std::vector::at() and throws on an out-of-range line or position,
// whereas the Scintilla Document NotepadNext really lexes over returns a default instead
// (PerLine.cxx LineLevels::GetLevel -> FoldLevel::Base, LineState::GetLineState -> 0,
// CellBuffer::StyleAt / UCharAt -> 0). ScintillaLikeDocument restores those semantics so only
// faults Scintilla itself would also hit are reported.
// Then it is re-lexed from the start of a middle line, as an edit-triggered incremental restyle does.
//
// State: each input creates and releases its own lexer and document; nothing is kept across inputs.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ILexer.h"
#include "Lexilla.h"
#include "Scintilla.h"
#include "TestDocument.h"

namespace {

// Folding and lexer options common to many lexers; each is switched on by one input bit.
const char *const kProps[8] = {
    "fold.comment", "fold.compact", "fold.preprocessor", "fold.html",
    "lexer.cpp.track.preprocessor", "fold.at.else", "lexer.html.mako", "styling.within.preprocessor",
};

// Generic keyword lists so keyword-dependent code paths (WordList lookups) are reached.
const char *const kWords[4] = {
    "if else for while do return int char void class struct function end then begin local nil true false",
    "include define ifdef ifndef endif pragma import from def lambda",
    "string table math print echo var let const",
    "SELECT FROM WHERE INSERT UPDATE DELETE",
};

// TestDocument with Scintilla Document's out-of-range behaviour (see the header comment).
class ScintillaLikeDocument : public TestDocument {
public:
    int SCI_METHOD GetLevel(Sci_Position line) const override {
        try {
            return TestDocument::GetLevel(line);
        } catch (const std::out_of_range &) {
            return SC_FOLDLEVELBASE;  // LineLevels::GetLevel
        }
    }
    int SCI_METHOD SetLevel(Sci_Position line, int level) override {
        try {
            return TestDocument::SetLevel(line, level);
        } catch (const std::out_of_range &) {
            return level;  // LineLevels::SetLevel ignores an out-of-range line
        }
    }
    int SCI_METHOD GetLineState(Sci_Position line) const override {
        try {
            return TestDocument::GetLineState(line);
        } catch (const std::out_of_range &) {
            return 0;  // LineState::GetLineState
        }
    }
    int SCI_METHOD SetLineState(Sci_Position line, int state) override {
        try {
            return TestDocument::SetLineState(line, state);
        } catch (const std::out_of_range &) {
            return state;  // LineState::SetLineState ignores an out-of-range line
        }
    }
    char SCI_METHOD StyleAt(Sci_Position position) const override {
        try {
            return TestDocument::StyleAt(position);
        } catch (const std::out_of_range &) {
            return 0;  // CellBuffer::StyleAt -> SplitVector::ValueAt
        }
    }
    int SCI_METHOD GetCharacterAndWidth(Sci_Position position, Sci_Position *pWidth) const override {
        try {
            return TestDocument::GetCharacterAndWidth(position, pWidth);
        } catch (const std::out_of_range &) {
            // A UTF-8 lead byte truncated by the document end: Scintilla reads the missing trail
            // bytes as 0 (CellBuffer::UCharAt), classifies the sequence invalid and reports the
            // lead byte as a width-1 singleton surrogate (Document::GetCharacterAndWidth).
            char lead = 0;
            GetCharRange(&lead, position, 1);
            if (pWidth)
                *pWidth = 1;
            return 0xDC80 + static_cast<unsigned char>(lead);
        }
    }
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2)
        return -1;
    const int count = GetLexerCount();
    if (count <= 0)
        return 0;
    char name[128] = {0};
    GetLexerName(static_cast<unsigned int>(data[0] % count), name, sizeof(name));
    Scintilla::ILexer5 *lexer = CreateLexer(name);
    if (!lexer)
        return 0;

    const uint8_t bits = data[1];
    lexer->PropertySet("fold", "1");
    for (int i = 0; i < 8; i++)
        lexer->PropertySet(kProps[i], (bits & (1u << i)) ? "1" : "0");
    for (int i = 0; i < 4; i++)
        lexer->WordListSet(i, kWords[i]);

    const std::string_view text(reinterpret_cast<const char *>(data + 2), size - 2);
    ScintillaLikeDocument doc;
    doc.Set(text);
    Scintilla::IDocument *pdoc = &doc;
    const Sci_Position length = pdoc->Length();

    lexer->Lex(0, length, 0, pdoc);
    lexer->Fold(0, length, 0, pdoc);

    // Incremental restyle from the start of the middle line, with the initial style Scintilla would
    // pass: the style just before that line.
    const Sci_Position midLine = pdoc->LineFromPosition(length / 2);
    const Sci_Position start = pdoc->LineStart(midLine);
    const int initStyle = start > 0 ? static_cast<unsigned char>(pdoc->StyleAt(start - 1)) : 0;
    lexer->Lex(start, length - start, initStyle, pdoc);
    lexer->Fold(start, length - start, initStyle, pdoc);

    lexer->Release();
    return 0;
}
