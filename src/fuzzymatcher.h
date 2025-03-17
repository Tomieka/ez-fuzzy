#ifndef FUZZYMATCHER_H
#define FUZZYMATCHER_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QPair>
#include <QHash>
#include <QFileInfo>
#include <QtConcurrent>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <mutex>

struct FileEntry {
    QString fullPath;
    QString fileName;
    QString lowerName;  // Pre-computed lowercase name for case-insensitive matching
};

class FuzzyMatcher
{
public:
    FuzzyMatcher();
    
    void setCollection(const QStringList &collection);
    
    QStringList search(const QString &query, int maxResults = 10) const;

private:
    int calculateScore(const QString &queryLower, const FileEntry &entry) const;
    
    int levenshteinDistance(const QString &s1, const QString &s2) const;
    
    void scoreFileBatch(const QString &queryLower, 
                        const QVector<FileEntry> &batch, 
                        QVector<QPair<QString, int>> &results,
                        std::mutex &resultsMutex) const;
    
    QVector<FileEntry> m_entries;
    
    mutable QHash<QString, QStringList> m_queryCache;
    mutable std::mutex m_cacheMutex;
};

#endif // FUZZYMATCHER_H 