#include "fuzzymatcher.h"
#include <QFileInfo>
#include <QtConcurrent>
#include <QDebug>
#include <QRegularExpression>

FuzzyMatcher::FuzzyMatcher()
{
}

void FuzzyMatcher::setCollection(const QStringList &collection)
{
    m_queryCache.clear();
    
    if (collection.isEmpty()) {
        m_entries.clear();
        return;
    }
    
    m_entries.clear();
    m_entries.reserve(collection.size());
    
    QVector<FileEntry> entries;
    entries.resize(collection.size());
    
    QtConcurrent::blockingMap(collection, [&entries, &collection](const QString &path) {
        int index = collection.indexOf(path);
        QFileInfo fileInfo(path);
        
        entries[index].fullPath = path;
        entries[index].fileName = fileInfo.fileName();
        entries[index].lowerName = fileInfo.fileName().toLower();
    });
    
    m_entries = entries;
}

QStringList FuzzyMatcher::search(const QString &query, int maxResults) const
{
    const QString queryLower = query.toLower();
    
    if (queryLower.isEmpty()) {
        QStringList results;
        results.reserve(qMin(maxResults, m_entries.size()));
        
        for (int i = 0; i < qMin(maxResults, m_entries.size()); ++i) {
            results.append(m_entries.at(i).fullPath);
        }
        return results;
    }
    
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (m_queryCache.contains(queryLower)) {
            QStringList cachedResults = m_queryCache.value(queryLower);
            return cachedResults.mid(0, maxResults);
        }
    }
    
    if (m_entries.isEmpty()) {
        return QStringList();
    }
    
    const int batchSize = 1000;
    QVector<QVector<FileEntry>> batches;
    
    for (int i = 0; i < m_entries.size(); i += batchSize) {
        int end = qMin(i + batchSize, m_entries.size());
        batches.append(m_entries.mid(i, end - i));
    }
    
    QVector<QPair<QString, int>> scoredStrings;
    std::mutex resultsMutex;
    
    QtConcurrent::blockingMap(batches, [this, &queryLower, &scoredStrings, &resultsMutex](const QVector<FileEntry> &batch) {
        scoreFileBatch(queryLower, batch, scoredStrings, resultsMutex);
    });
    
    std::sort(scoredStrings.begin(), scoredStrings.end(), 
              [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                  return a.second > b.second;
              });
    
    QStringList results;
    int count = qMin(maxResults, scoredStrings.size());
    results.reserve(count);
    
    for (int i = 0; i < count; ++i) {
        results.append(scoredStrings[i].first);
    }
    
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (!results.isEmpty() && m_queryCache.size() < 1000) {
            m_queryCache[queryLower] = results;
        }
    }
    
    return results;
}

void FuzzyMatcher::scoreFileBatch(const QString &queryLower, 
                                  const QVector<FileEntry> &batch, 
                                  QVector<QPair<QString, int>> &results,
                                  std::mutex &resultsMutex) const
{
    QVector<QPair<QString, int>> localResults;
    localResults.reserve(batch.size() / 2);
    
    for (const FileEntry &entry : batch) {
        int score = calculateScore(queryLower, entry);
        if (score > 0) {
            localResults.append(qMakePair(entry.fullPath, score));
        }
    }
    
    if (!localResults.isEmpty()) {
        std::lock_guard<std::mutex> lock(resultsMutex);
        results.append(localResults);
    }
}

int FuzzyMatcher::calculateScore(const QString &queryLower, const FileEntry &entry) const
{
    if (queryLower.isEmpty()) {
        return 1;
    }
    
    if (entry.lowerName == queryLower) {
        return 1000;
    }
    
    if (entry.lowerName.startsWith(queryLower)) {
        return 800;
    }
    
    if (entry.lowerName.contains(queryLower)) {
        return 600;
    }
    
    if (queryLower.length() <= 5 && queryLower.length() >= 2) {
        QString pattern = entry.lowerName;
        pattern.replace(QRegularExpression("[^a-zA-Z0-9]"), " ");
        QStringList words = pattern.split(" ", Qt::SkipEmptyParts);
        
        if (words.size() >= queryLower.length()) {
            bool match = true;
            for (int i = 0; i < queryLower.length() && i < words.size(); ++i) {
                if (words[i].isEmpty() || words[i][0] != queryLower[i]) {
                    match = false;
                    break;
                }
            }
            
            if (match) {
                return 550;
            }
        }
    }
    
    if (queryLower.length() > 2 || entry.lowerName.length() < 10) {
        int distance = levenshteinDistance(queryLower, entry.lowerName);
        
        if (distance <= queryLower.length() * 2) {
            int levenshteinScore = 500 - distance * 10;
            
            return qMax(1, levenshteinScore);
        }
    }
    
    int lastIndex = -1;
    bool allCharsFound = true;
    
    for (QChar c : queryLower) {
        int index = entry.lowerName.indexOf(c, lastIndex + 1);
        if (index == -1) {
            allCharsFound = false;
            break;
        }
        lastIndex = index;
    }
    
    if (allCharsFound) {
        int matchLength = lastIndex + 1 - entry.lowerName.indexOf(queryLower[0]);
        int proximityScore = 100 - qMin(90, matchLength);
        return proximityScore;
    }
    
    return 0;
}

int FuzzyMatcher::levenshteinDistance(const QString &s1, const QString &s2) const
{
    const int len1 = s1.length();
    const int len2 = s2.length();
    
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;
    
    if (qAbs(len1 - len2) > len1) {
        return len2;
    }
    
    QVector<int> prev(len2 + 1);
    QVector<int> curr(len2 + 1);
    
    for (int i = 0; i <= len2; i++) {
        prev[i] = i;
    }
    
    for (int i = 0; i < len1; i++) {
        curr[0] = i + 1;
        
        bool allBig = true;
        const int threshold = len1;
        
        for (int j = 0; j < len2; j++) {
            int cost = (s1[i] == s2[j]) ? 0 : 1;
            curr[j + 1] = qMin(qMin(curr[j] + 1, prev[j + 1] + 1), prev[j] + cost);
            
            if (curr[j + 1] < threshold) {
                allBig = false;
            }
        }
        
        if (allBig) {
            return threshold;
        }
        
        prev = curr;
    }
    
    return prev[len2];
} 