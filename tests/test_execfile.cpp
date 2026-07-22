// Tests fuer die Erkennung ausfuehrbarer Dateien (Farbmarkierung).
// (Port von tests/test_execfile.py)
#include "tests/harness.hpp"

#include "ncssh/core/execfile.hpp"
#include "ncssh/core/models.hpp"

using namespace ncssh::core;

namespace {
FileEntry makeFile(const char *name, quint32 perm = 0)
{
    FileEntry e;
    e.name = QString::fromUtf8(name);
    e.type = EntryType::File;
    e.permissions = perm;
    return e;
}
} // namespace

TEST(execfile, posix_exec_bit)
{
    CHECK(isExecutable(makeFile("run", 0755)));
    CHECK(isExecutable(makeFile("daemon", 0700)));   // nur owner-x reicht
    CHECK(!isExecutable(makeFile("readme", 0644)));
}

TEST(execfile, windows_extensions)
{
    for (const char *n : {"setup.exe", "Install.MSI", "go.bat", "task.cmd", "deploy.ps1"})
        CHECK(isExecutable(makeFile(n)));
    CHECK(!isExecutable(makeFile("notes.txt")));
    CHECK(!isExecutable(makeFile("photo.PNG")));
}

TEST(execfile, non_files_never_executable)
{
    FileEntry dir;
    dir.name = QStringLiteral("bin");
    dir.type = EntryType::Dir;
    dir.permissions = 0755;
    CHECK(!isExecutable(dir));

    FileEntry parent;
    parent.name = QStringLiteral("..");
    parent.type = EntryType::Parent;
    CHECK(!isExecutable(parent));

    FileEntry link;
    link.name = QStringLiteral("link");
    link.type = EntryType::Symlink;
    link.permissions = 0777;
    CHECK(!isExecutable(link));
}
