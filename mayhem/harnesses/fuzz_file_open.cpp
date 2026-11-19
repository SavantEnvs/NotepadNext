// Fuzz opening a file in NotepadNext: ScintillaNext::fromFile(path) -> readFromDisk(), the path every
// File > Open, drag-and-drop, command-line file and session restore takes. It reads the file in
// CHUNK_SIZE pieces, detects a UTF-8 / UTF-16LE / UTF-16BE byte-order mark, decodes UTF-16 to UTF-8
// with QStringDecoder and appends each chunk to the Scintilla document (CellBuffer). Upstream fixed
// crashes here for 0/1-byte files and for non-UTF-16 data after a UTF-16 BOM.
//
// The input bytes are the file content: written to a fresh temp file (mkstemp under $TMPDIR), opened
// with ScintillaNext::fromFile, then the loaded document is read back and walked line by line.
// State: one offscreen QApplication (created once, never touched by an input); each input gets its
// own editor and temp file, both deleted at the end.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <unistd.h>

#include "ScintillaNext.h"
#include "qt_fuzz_common.h"

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    nnFuzzInitQt();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const char *tmp = getenv("TMPDIR");
    std::string path = std::string(tmp && *tmp ? tmp : "/tmp") + "/nn-fuzz-open-XXXXXX";
    int fd = mkstemp(path.data());
    if (fd < 0)
        return 0;
    size_t off = 0;
    while (off < size) {
        ssize_t n = write(fd, data + off, size - off);
        if (n <= 0)
            break;
        off += static_cast<size_t>(n);
    }
    close(fd);

    ScintillaNext *editor = ScintillaNext::fromFile(QString::fromLocal8Bit(path.c_str()));
    if (editor) {
        const sptr_t length = editor->length();
        const sptr_t lines = editor->lineCount();
        volatile sptr_t sink = 0;
        for (sptr_t line = 0; line < lines; line++)
            sink += editor->lineEndPosition(line) - editor->positionFromLine(line);
        const QByteArray text = editor->getText(length + 1);
        sink += text.size();
        (void)sink;
        delete editor;
    }
    unlink(path.c_str());
    return 0;
}
