// Einstiegspunkt (Port von __main__.py -> gui/app.py::main)
#include "ncssh/gui/app.hpp"

int main(int argc, char *argv[])
{
    return ncssh::gui::appMain(argc, argv);
}
