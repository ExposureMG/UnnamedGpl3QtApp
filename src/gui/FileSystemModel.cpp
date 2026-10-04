#include "gui/FileSystemModel.hpp"

#include <QDateTime>
#include <QLocale>

#include <algorithm>

namespace unnamed::gui {

FileSystemModel::FileSystemModel(QObject* parent) : QAbstractListModel(parent) {}

void FileSystemModel::setEntries(const QString& path, std::vector<core::Entry> entries) {
    m_path = path;
    m_all = std::move(entries);
    rebuild();
}

void FileSystemModel::clear() {
    m_path = QStringLiteral("/");
    m_all.clear();
    rebuild();
}

void FileSystemModel::setSortKey(SortKey key) {
    if (m_sortKey == key)
        return;
    m_sortKey = key;
    rebuild();
    emit sortChanged();
}

void FileSystemModel::setSortAscending(bool ascending) {
    if (m_sortAscending == ascending)
        return;
    m_sortAscending = ascending;
    rebuild();
    emit sortChanged();
}

void FileSystemModel::setFilterText(const QString& text) {
    if (m_filterText == text)
        return;
    m_filterText = text;
    rebuild();
    emit filterTextChanged();
}

int FileSystemModel::rowForName(const QString& name) const {
    const std::string utf8 = name.toStdString();
    for (size_t i = 0; i < m_view.size(); ++i) {
        if (m_view[i].name == utf8)
            return static_cast<int>(i);
    }
    return -1;
}

void FileSystemModel::rebuild() {
    std::vector<core::Entry> view;
    view.reserve(m_all.size());
    for (const core::Entry& e : m_all) {
        if (m_filterText.isEmpty() ||
            QString::fromStdString(e.name).contains(m_filterText, Qt::CaseInsensitive))
            view.push_back(e);
    }

    auto byName = [](const core::Entry& a, const core::Entry& b) {
        return QString::compare(QString::fromStdString(a.name), QString::fromStdString(b.name),
                                Qt::CaseInsensitive);
    };
    const bool asc = m_sortAscending;
    const SortKey key = m_sortKey;
    std::stable_sort(view.begin(), view.end(), [&](const core::Entry& a, const core::Entry& b) {
        // Folders always come first, whatever the direction.
        if (a.type != b.type)
            return a.type == core::EntryType::Directory;
        int cmp = 0;
        switch (key) {
        case SortByName: cmp = byName(a, b); break;
        case SortByKind:
            cmp = a.kind.compare(b.kind);
            if (cmp == 0)
                cmp = byName(a, b);
            break;
        case SortBySize:
            cmp = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
            if (cmp == 0)
                cmp = byName(a, b);
            break;
        case SortByModified:
            cmp = a.modified < b.modified ? -1 : (a.modified > b.modified ? 1 : 0);
            if (cmp == 0)
                cmp = byName(a, b);
            break;
        }
        return asc ? cmp < 0 : cmp > 0;
    });

    beginResetModel();
    m_view = std::move(view);
    endResetModel();
    emit countChanged();
}

int FileSystemModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant FileSystemModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};

    const core::Entry& entry = entryAt(index.row());
    const bool isDir = entry.type == core::EntryType::Directory;
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return QString::fromStdString(entry.name);
    case IsDirectoryRole:
        return isDir;
    case KindRole:
        return QString::fromStdString(entry.kind);
    case KindLabelRole:
        return QString::fromStdString(core::kindLabel(entry.kind));
    case SizeTextRole:
        return isDir ? QString() : QLocale().formattedDataSize(static_cast<qint64>(entry.size));
    case ModifiedTextRole:
        return entry.modified > 0
                   ? QLocale().toString(QDateTime::fromSecsSinceEpoch(entry.modified),
                                        QLocale::ShortFormat)
                   : QString();
    }
    return {};
}

QHash<int, QByteArray> FileSystemModel::roleNames() const {
    return {
        {NameRole, "name"},
        {IsDirectoryRole, "isDirectory"},
        {KindRole, "kind"},
        {KindLabelRole, "kindLabel"},
        {SizeTextRole, "sizeText"},
        {ModifiedTextRole, "modifiedText"},
    };
}

} // namespace unnamed::gui
