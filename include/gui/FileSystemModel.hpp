#pragma once

#include "core/FileSystem.hpp"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

namespace unnamed::gui {

// List model of one directory listing, exposed to QML. Handles
// sorting (folders always first) and name filtering.
class FileSystemModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by FileBrowser")
    Q_PROPERTY(SortKey sortKey READ sortKey WRITE setSortKey NOTIFY sortChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending WRITE setSortAscending NOTIFY sortChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        IsDirectoryRole,
        KindRole,
        KindLabelRole,
        SizeTextRole,
        ModifiedTextRole,
    };

    enum SortKey { SortByName, SortByKind, SortBySize, SortByModified };
    Q_ENUM(SortKey)

    explicit FileSystemModel(QObject* parent = nullptr);

    // Replaces the contents with the listing of `path` (done by FileBrowser once
    // the background listing has finished).
    void setEntries(const QString& path, std::vector<core::Entry> entries);
    void clear();
    QString path() const { return m_path; }

    SortKey sortKey() const { return m_sortKey; }
    void setSortKey(SortKey key);
    bool sortAscending() const { return m_sortAscending; }
    void setSortAscending(bool ascending);
    QString filterText() const { return m_filterText; }
    void setFilterText(const QString& text);

    int count() const { return static_cast<int>(m_view.size()); }
    int totalCount() const { return static_cast<int>(m_all.size()); }

    const core::Entry& entryAt(int row) const { return m_view.at(static_cast<size_t>(row)); }
    // Row of the entry called `name` in the current view, or -1.
    Q_INVOKABLE int rowForName(const QString& name) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void sortChanged();
    void filterTextChanged();
    void countChanged();

private:
    void rebuild();

    QString m_path = QStringLiteral("/");
    std::vector<core::Entry> m_all;  // as listed by the filesystem
    std::vector<core::Entry> m_view; // filtered + sorted
    SortKey m_sortKey = SortByName;
    bool m_sortAscending = true;
    QString m_filterText;
};

} // namespace unnamed::gui
