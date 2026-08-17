// Shell-Backends: verbinden ein TerminalWidget mit einem echten PTY.
//
// LocalShellBackend  : lokale Shell ueber Windows ConPTY,
//                      gelesen in einem Hintergrund-Thread.
// RemoteShellBackend : interaktiver SSH-Shell-Channel ueber net::RemoteShell.
//
// Beide melden empfangene Daten als Qt-Signal dataReceived(QString) (thread-
// sicher an die GUI), nehmen Eingaben ueber write(QString) und passen die
// Groesse via resize(cols, rows) an.
#pragma once

#include "ncssh/gui/bridge.hpp"
#include "ncssh/net/ssh.hpp"

#include <QObject>
#include <atomic>
#include <memory>
#include <thread>

namespace ncssh::gui {

class ShellBackend : public QObject {
    Q_OBJECT
public:
    explicit ShellBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~ShellBackend() override = default;

    virtual void write(const QString &text) = 0;
    virtual void resize(int cols, int rows) = 0;
    virtual void close() = 0;

signals:
    void dataReceived(const QString &data);
    void closed();
};

class LocalShellBackend : public ShellBackend {
    Q_OBJECT
public:
    explicit LocalShellBackend(QObject *parent = nullptr);
    ~LocalShellBackend() override;

    // Startet die lokale Shell (COMSPEC bzw. $SHELL) mit der Groesse cols x rows.
    void start(int cols, int rows);

    void write(const QString &text) override;
    void resize(int cols, int rows) override;
    void close() override;

private:
    void readLoop();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::atomic_bool m_alive{false};
    std::atomic_bool m_closing{false};   // close() darf nur einmal abbauen
    std::thread m_thread;
};

class RemoteShellBackend : public ShellBackend {
    Q_OBJECT
public:
    explicit RemoteShellBackend(AsyncBridge *bridge, QObject *parent = nullptr);
    ~RemoteShellBackend() override;

    void start(const net::SSHSessionPtr &session, int cols, int rows);

    void write(const QString &text) override;
    void resize(int cols, int rows) override;
    void close() override;

private:
    AsyncBridge *m_bridge;
    std::unique_ptr<net::RemoteShell> m_shell;
    std::atomic_bool m_alive{false};
    std::thread m_thread;
};

} // namespace ncssh::gui
