#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QTimer>
#include <QDir>
#include <QThread>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QProgressDialog>
#include <QSettings>
#include <QCompleter>
#include <QStringListModel>
#include <QAction>
#include <QMenu>
#include <QComboBox>
#include <QStyledItemDelegate>
#include <QShortcut>
#include <QSplitter>
#include <QTextEdit>
#include <QProcess>
#include <QCheckBox>
#include "fuzzymatcher.h"
#include "syntaxhighlighter.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class DirectoryScanner : public QObject
{
    Q_OBJECT
public:
    explicit DirectoryScanner(QObject *parent = nullptr) : QObject(parent) {}
    
    QStringList scanDirectory(const QString &path);

signals:
    void scanProgress(int filesFound);
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onSearchTextChanged();
    void onBrowseClicked();
    void onItemDoubleClicked(QListWidgetItem *item);
    void performSearch();
    void onScanFinished();
    void onScanProgress(int filesFound);
    void onNextClicked();
    void onPrevClicked();
    void onSearchFinished();
    void onFilterByTypeChanged(int index);
    void onDarkThemeToggled(bool checked);
    void onAddBookmark();
    void onRemoveBookmark();
    void onBookmarkSelected(int index);
    
    void showContextMenu(const QPoint &pos);
    void openSelectedFile();
    void openContainingFolder();
    void copyFullPath();
    void copyFileName();
    void copyRelativePath();
    
    void previewSelectedFile();
    void togglePreviewPane(bool checked);
    
    void handleKeyUp();
    void handleKeyDown();
    void handleKeyEnter();
    void handleKeyEscape();
    
    void onIgnorePatternChanged();
    
    void onShowFilesToggled(bool checked);
    void onShowDirectoriesToggled(bool checked);

private:
    Ui::MainWindow *ui;
    FuzzyMatcher m_fuzzyMatcher;
    QStringList m_fileList;
    QTimer m_searchTimer;
    QString m_currentDir;
    QFutureWatcher<QStringList> *m_scanWatcher;
    QProgressDialog *m_progressDialog;
    QThread m_workerThread;
    DirectoryScanner *m_directoryScanner;
    
    QStringList m_allResults;
    int m_currentPage;
    int m_totalPages;
    static const int PAGE_SIZE = 200;
    
    QSettings m_settings;
    QStringList m_searchHistory;
    QCompleter *m_completer;
    QStringListModel *m_historyModel;
    static const int MAX_HISTORY_ITEMS = 20;
    
    QComboBox *m_fileTypeFilter;
    QStringList m_fileExtensions;
    QStringList m_filteredResults;
    QString m_currentFilter;
    
    QComboBox *m_bookmarksCombo;
    QPushButton *m_addBookmarkButton;
    QPushButton *m_removeBookmarkButton;
    QStringList m_bookmarks;
    
    QAction *m_darkThemeAction;
    bool m_isDarkTheme;
    
    QMenu *m_contextMenu;
    QAction *m_openAction;
    QAction *m_openFolderAction;
    QAction *m_copyPathAction;
    QAction *m_copyNameAction;
    QAction *m_copyRelativePathAction;
    
    QSplitter *m_mainSplitter;
    QTextEdit *m_previewTextEdit;
    QAction *m_previewAction;
    bool m_previewEnabled;
    QTimer m_previewTimer;
    
    QShortcut *m_upShortcut;
    QShortcut *m_downShortcut;
    QShortcut *m_enterShortcut;
    QShortcut *m_escShortcut;
    
    QLineEdit *m_ignorePatternEdit;
    QStringList m_ignorePatterns;
    
    QCheckBox *m_showFilesCheckbox;
    QCheckBox *m_showDirsCheckbox;
    bool m_showFiles;
    bool m_showDirectories;
    
    SyntaxHighlighter *m_highlighter;
    
    void updateResults(const QStringList &results);
    void updatePaginationControls();
    void displayCurrentPage();
    void createProgressDialog();
    
    void loadSettings();
    void saveSettings();
    void addToSearchHistory(const QString &searchTerm);
    void updateCompleter();
    
    void setupFileTypeFilter();
    void setupBookmarks();
    void setupThemeSupport();
    void setupContextMenu();
    void setupPreviewPane();
    void setupKeyboardShortcuts();
    void setupIgnorePatterns();
    void applyFilter();
    void applyTheme();
    void updateBookmarks();
    QStringList getFileTypeExtensions(const QStringList &files);
    QString getSelectedFilePath() const;
    QString getFilePreview(const QString &filePath, int maxLines = 200) const;
    bool isFileTypeText(const QString &filePath) const;
    bool shouldIgnoreFile(const QString &filePath) const;
    void setupFileTypeCheckboxes();
    void applyFileTypeFilter();
    void calculateTotalPages();
    bool isDirectory(const QString &path) const;
};

#endif 