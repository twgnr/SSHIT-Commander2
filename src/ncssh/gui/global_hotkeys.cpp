#include "ncssh/gui/global_hotkeys.hpp"

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace ncssh::gui {

#ifdef Q_OS_WIN

namespace {

constexpr wchar_t kWindowClass[] = L"SSHITCommanderGlobalHotkeys";

LRESULT CALLBACK hotkeyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_HOTKEY) {
        if (auto *self = reinterpret_cast<GlobalHotkeys *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
            emit self->activated(int(wParam));
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool registerWindowClass()
{
    static const bool ok = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = hotkeyWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kWindowClass;
        return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    return ok;
}

unsigned virtualKey(Qt::Key key, Qt::KeyboardModifiers mods)
{
    const bool keypad = mods.testFlag(Qt::KeypadModifier);
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return 'A' + (key - Qt::Key_A);
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return keypad ? VK_NUMPAD0 + (key - Qt::Key_0) : '0' + (key - Qt::Key_0);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return VK_F1 + (key - Qt::Key_F1);
    switch (key) {
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Return:
    case Qt::Key_Enter: return VK_RETURN;
    case Qt::Key_Escape: return VK_ESCAPE;
    case Qt::Key_Tab:
    case Qt::Key_Backtab: return VK_TAB;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Delete: return VK_DELETE;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    case Qt::Key_Print: return VK_SNAPSHOT;
    case Qt::Key_Pause: return VK_PAUSE;
    case Qt::Key_ScrollLock: return VK_SCROLL;
    case Qt::Key_VolumeUp: return VK_VOLUME_UP;
    case Qt::Key_VolumeDown: return VK_VOLUME_DOWN;
    case Qt::Key_VolumeMute: return VK_VOLUME_MUTE;
    case Qt::Key_MediaPlay:
    case Qt::Key_MediaTogglePlayPause: return VK_MEDIA_PLAY_PAUSE;
    case Qt::Key_MediaNext: return VK_MEDIA_NEXT_TRACK;
    case Qt::Key_MediaPrevious: return VK_MEDIA_PREV_TRACK;
    case Qt::Key_MediaStop: return VK_MEDIA_STOP;
    default: break;
    }
    if (keypad) {
        switch (key) {
        case Qt::Key_Asterisk: return VK_MULTIPLY;
        case Qt::Key_Plus: return VK_ADD;
        case Qt::Key_Minus: return VK_SUBTRACT;
        case Qt::Key_Slash: return VK_DIVIDE;
        case Qt::Key_Period:
        case Qt::Key_Comma: return VK_DECIMAL;
        default: break;
        }
    }
    // Uebrige Zeichen (Satzzeichen, Umlaute, "!" aus Umschalt+1 …) ueber das
    // aktuelle Tastaturlayout; die Umschalt-Stufe kommt aus den Modifikatoren.
    if (key >= Qt::Key_Space && key <= 0xffff) {
        const QChar ch = QChar(char16_t(key)).toLower();
        const SHORT vk = VkKeyScanW(wchar_t(ch.unicode()));
        if (vk != -1)
            return unsigned(vk & 0xFF);
    }
    return 0;
}

} // namespace

GlobalHotkeys::GlobalHotkeys(QObject *parent) : QObject(parent)
{
    if (!registerWindowClass())
        return;
    HWND hwnd = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    if (!hwnd)
        return;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    m_hwnd = reinterpret_cast<quintptr>(hwnd);
}

GlobalHotkeys::~GlobalHotkeys()
{
    clear();
    if (m_hwnd) {
        HWND hwnd = reinterpret_cast<HWND>(m_hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        DestroyWindow(hwnd);
    }
}

bool GlobalHotkeys::add(int id, const QKeySequence &seq)
{
    // Anwendungs-IDs muessen unter 0xC000 liegen.
    if (!m_hwnd || id < 0 || id >= 0xC000)
        return false;
    const auto native = toNative(seq);
    if (!native)
        return false;
    if (!RegisterHotKey(reinterpret_cast<HWND>(m_hwnd), id, native->first | MOD_NOREPEAT,
                        native->second))
        return false;
    m_ids.append(id);
    return true;
}

void GlobalHotkeys::clear()
{
    for (int id : std::as_const(m_ids))
        UnregisterHotKey(reinterpret_cast<HWND>(m_hwnd), id);
    m_ids.clear();
}

std::optional<std::pair<unsigned, unsigned>> GlobalHotkeys::toNative(const QKeySequence &seq)
{
    if (seq.isEmpty())
        return std::nullopt;
    const QKeyCombination combo = seq[0];
    const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
    const unsigned vk = virtualKey(combo.key(), mods);
    if (!vk)
        return std::nullopt;
    unsigned native = 0;
    if (mods.testFlag(Qt::ControlModifier))
        native |= MOD_CONTROL;
    if (mods.testFlag(Qt::ShiftModifier))
        native |= MOD_SHIFT;
    if (mods.testFlag(Qt::AltModifier))
        native |= MOD_ALT;
    if (mods.testFlag(Qt::MetaModifier))
        native |= MOD_WIN;
    return std::make_pair(native, vk);
}

bool GlobalHotkeys::modifiersHeld()
{
    for (int vk : {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN}) {
        if (GetAsyncKeyState(vk) & 0x8000)
            return true;
    }
    return false;
}

#else // !Q_OS_WIN

GlobalHotkeys::GlobalHotkeys(QObject *parent) : QObject(parent) {}
GlobalHotkeys::~GlobalHotkeys() = default;
bool GlobalHotkeys::add(int, const QKeySequence &) { return false; }
void GlobalHotkeys::clear() {}
std::optional<std::pair<unsigned, unsigned>> GlobalHotkeys::toNative(const QKeySequence &)
{
    return std::nullopt;
}
bool GlobalHotkeys::modifiersHeld() { return false; }

#endif

} // namespace ncssh::gui
