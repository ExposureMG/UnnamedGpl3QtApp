#pragma once

#include "core/FileSystemRegistry.hpp"
#include "gui/FileSystemModel.hpp"

#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace unnamed::gui {

// QML-facing singleton controller: owns the open filesystem, current directory and
// navigation state. The Kirigami UI binds to this and never touches core::
// types directly.
class FileBrowser : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(unnamed::gui::FileSystemModel* model READ model CONSTANT)
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY stateChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY stateChanged)
    Q_PROPERTY(QString path READ path NOTIFY stateChanged)
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY stateChanged)
    Q_PROPERTY(QString rootName READ rootName NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
public:
    explicit FileBrowser(QObject* parent = nullptr);

    FileSystemModel* model() const { return m_model; }
    bool isOpen() const { return m_model->fileSystem() != nullptr; }
    bool canGoUp() const { return isOpen() && m_model->path() != QStringLiteral("/"); }
    QString path() const { return m_model->path(); }
    // Host folder that is open, and its display name (empty when nothing is open).
    QString rootPath() const { return m_rootPath; }
    QString rootName() const;
    QString statusText() const;
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE void openFolder(const QUrl& folder);
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void goUp();

signals:
    void stateChanged();
    void errorMessageChanged();

private:
    void navigate(const QString& path);
    void setError(const QString& message);

    core::FileSystemRegistry m_registry;
    FileSystemModel* m_model;
    QString m_rootPath;
    QString m_errorMessage;
};

} // namespace unnamed::gui
