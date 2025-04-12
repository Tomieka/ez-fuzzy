#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QDirIterator>
#include <QDebug>
#include <QMessageBox>
#include <QClipboard>
#include <QApplication>
#include <QToolTip>
#include <atomic>
#include <QtConcurrent>
#include <mutex>
#include <QStyle>
#include <QPalette>
#include <QInputDialog>
#include <QShortcut>
#include <QFile>
#include <QTextStream>
#include <QMimeDatabase>
#include <QRegularExpression>
#include "syntaxhighlighter.h"

static std::atomic<int> g_filesScanned(0);

QStringList DirectoryScanner::scanDirectory(const QString &path)
{
    QStringList fileList;
    g_filesScanned = 0;
    
    std::mutex fileListMutex;

    QDir rootDir(path);
    QStringList topDirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    topDirs.prepend(".");
    
    QStringList rootFiles = rootDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    {
        std::lock_guard<std::mutex> lock(fileListMutex);
        for (const QString &entry : rootFiles) {
            fileList.append(rootDir.filePath(entry));
            g_filesScanned++;
        }
        
        for (const QString &entry : topDirs) {
            if (entry != ".") {  // Skip the "." we added ourselves
                fileList.append(rootDir.filePath(entry));
                g_filesScanned++;
            }
        }
    }
    
    QFuture<void> future = QtConcurrent::map(topDirs, [&](const QString &subdir) {
        QString fullSubdirPath = subdir == "." ? path : rootDir.filePath(subdir);
        QStringList threadLocalFiles;
        
        QDirIterator it(fullSubdirPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        
        const int batchSize = 1000;
        int batchCount = 0;
        
        while (it.hasNext()) {
            it.next();
            threadLocalFiles.append(it.filePath());
            
            int filesScanned = ++g_filesScanned;
            if (filesScanned % 1000 == 0) {
                emit scanProgress(filesScanned);
            }
            
            if (++batchCount >= batchSize) {
                std::lock_guard<std::mutex> lock(fileListMutex);
                fileList.append(threadLocalFiles);
                threadLocalFiles.clear();
                batchCount = 0;
            }
        }
        
        if (!threadLocalFiles.isEmpty()) {
            std::lock_guard<std::mutex> lock(fileListMutex);
            fileList.append(threadLocalFiles);
        }
    });
    
    future.waitForFinished();
    
    emit scanProgress(g_filesScanned);
    
    return fileList;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_scanWatcher(nullptr)
    , m_progressDialog(nullptr)
    , m_directoryScanner(nullptr)
    , m_currentPage(0)
    , m_totalPages(0)
    , m_settings("EZ-Fuzzy", "EZ-Fuzzy-Finder")
    , m_completer(nullptr)
    , m_historyModel(new QStringListModel(this))
    , m_isDarkTheme(false)
    , m_previewEnabled(true)
    , m_showFiles(true)
    , m_showDirectories(false)
{
    ui->setupUi(this);
    
    m_searchTimer.setSingleShot(true);
    connect(&m_searchTimer, &QTimer::timeout, this, &MainWindow::performSearch);
    
    m_previewTimer.setSingleShot(true);
    connect(&m_previewTimer, &QTimer::timeout, this, &MainWindow::previewSelectedFile);
    
    connect(ui->searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
    connect(ui->browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseClicked);
    connect(ui->resultsList, &QListWidget::itemDoubleClicked, this, &MainWindow::onItemDoubleClicked);
    connect(ui->nextButton, &QPushButton::clicked, this, &MainWindow::onNextClicked);
    connect(ui->prevButton, &QPushButton::clicked, this, &MainWindow::onPrevClicked);
    connect(ui->actionExit, &QAction::triggered, this, &QMainWindow::close);
    
    connect(ui->resultsList, &QListWidget::currentItemChanged, 
            [this](QListWidgetItem *current, QListWidgetItem *) {
                if (current && m_previewEnabled) {
                    m_previewTimer.start(100);
                }
            });
    
    connect(ui->ignorePatternEdit, &QLineEdit::editingFinished,
            this, &MainWindow::onIgnorePatternChanged);
    
    m_directoryScanner = new DirectoryScanner();
    m_directoryScanner->moveToThread(&m_workerThread);
    
    connect(&m_workerThread, &QThread::finished, m_directoryScanner, &QObject::deleteLater);
    connect(m_directoryScanner, &DirectoryScanner::scanProgress, this, &MainWindow::onScanProgress);
    
    m_workerThread.start(QThread::HighPriority);
    
    m_scanWatcher = new QFutureWatcher<QStringList>(this);
    connect(m_scanWatcher, &QFutureWatcher<QStringList>::finished, this, &MainWindow::onScanFinished);
    
    setWindowTitle("EZ Fuzzy File Finder");
    resize(900, 600);
    
    setupFileTypeFilter();
    setupBookmarks();
    setupThemeSupport();
    setupContextMenu();
    setupPreviewPane();
    setupKeyboardShortcuts();
    setupIgnorePatterns();
    setupFileTypeCheckboxes();
    
    loadSettings();
    
    m_completer = new QCompleter(m_historyModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    ui->searchEdit->setCompleter(m_completer);
    
    ui->pageInfoLabel->setText("No files indexed yet");
    ui->prevButton->setEnabled(false);
    ui->nextButton->setEnabled(false);
    
    ui->searchEdit->setFocus();

    m_highlighter = new SyntaxHighlighter(ui->previewTextEdit->document());
}

MainWindow::~MainWindow()
{
    saveSettings();
    
    m_workerThread.quit();
    m_workerThread.wait();
    
    delete ui;
    delete m_scanWatcher;
    delete m_completer;
    delete m_contextMenu;
    delete m_historyModel;
    delete m_openAction;
    delete m_openFolderAction;
    delete m_copyPathAction;
    delete m_copyNameAction; 
    delete m_copyRelativePathAction;
    delete m_upShortcut;
    delete m_downShortcut;
    delete m_enterShortcut;
    delete m_escShortcut;
    
    if (m_progressDialog) {
        delete m_progressDialog;
    }
}

void MainWindow::onSearchTextChanged()
{
    m_searchTimer.start(100);
}

void MainWindow::onBrowseClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select Directory",
                                                   m_currentDir.isEmpty() ? QDir::homePath() : m_currentDir,
                                                   QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    
    if (!dir.isEmpty()) {
        m_currentDir = dir;
        ui->searchEdit->clear();
        ui->resultsList->clear();
        
        QDir topDir(dir);
        QStringList topLevelFiles = topDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        int dirCount = topDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).count();
        
        ui->infoLabel->setText(QString("Directory: %1\nContains %2 files and %3 subdirectories at top level")
                              .arg(QDir(dir).dirName())
                              .arg(topLevelFiles.count())
                              .arg(dirCount));
        
        statusBar()->showMessage(QString("Scanning %1...").arg(dir));
        
        ui->browseButton->setEnabled(false);
        ui->searchEdit->setEnabled(false);
        
        createProgressDialog();
        
        QFuture<QStringList> future = QtConcurrent::run(m_directoryScanner, &DirectoryScanner::scanDirectory, dir);
        m_scanWatcher->setFuture(future);
        
        m_settings.setValue("lastDirectory", dir);
    }
}

void MainWindow::createProgressDialog()
{
    if (m_progressDialog) {
        disconnect(m_progressDialog, &QProgressDialog::canceled, nullptr, nullptr);
        delete m_progressDialog;
    }
    
    m_progressDialog = new QProgressDialog("Scanning directory...", "Cancel", 0, 0, this);
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setValue(0);
    m_progressDialog->show();
    
    connect(m_progressDialog, &QProgressDialog::canceled, [this]() {
        m_scanWatcher->cancel();
        ui->browseButton->setEnabled(true);
        ui->searchEdit->setEnabled(true);
        statusBar()->showMessage("Scan canceled", 3000);
    });
}

void MainWindow::onScanProgress(int filesFound)
{
    if (m_progressDialog) {
        m_progressDialog->setLabelText(QString("Scanning directory... Found %1 files").arg(filesFound));
    }
    
    QDir dirInfo(m_currentDir);
    ui->infoLabel->setText(QString("Directory: %1\nContains files and subdirectories\nScanning... Found %2 files so far")
                          .arg(dirInfo.dirName())
                          .arg(filesFound));
}

void MainWindow::onScanFinished()
{
    if (!m_scanWatcher->isCanceled()) {
        m_fileList = m_scanWatcher->result();
        
        if (!m_ignorePatterns.isEmpty()) {
            QStringList filteredFileList;
            for (const QString &file : m_fileList) {
                if (!shouldIgnoreFile(file)) {
                    filteredFileList.append(file);
                }
            }
            m_fileList = filteredFileList;
        }
        
        statusBar()->showMessage(QString("Found %1 files in %2").arg(m_fileList.size()).arg(m_currentDir));
        
        ui->infoLabel->setText(QString("Directory: %1\nFound %2 files in total (scan complete)")
                              .arg(QDir(m_currentDir).dirName())
                              .arg(m_fileList.size()));
        
        m_fuzzyMatcher.setCollection(m_fileList);
        
        m_fileExtensions = getFileTypeExtensions(m_fileList);
        setupFileTypeFilter();
        
        if (ui->searchEdit->text().isEmpty()) {
            updateResults(m_fileList);
        }
    }
    
    ui->browseButton->setEnabled(true);
    ui->searchEdit->setEnabled(true);
    
    if (m_progressDialog) {
        m_progressDialog->close();
    }
}

void MainWindow::onItemDoubleClicked(QListWidgetItem *item)
{
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) {
        return;
    }
    
    QString filePath = item->data(Qt::UserRole).toString();
    
    QApplication::clipboard()->setText(filePath);
    
    statusBar()->showMessage(QString("Path copied to clipboard: %1").arg(filePath), 5000);
    
    ui->infoLabel->setText(QString("Selected file: %1").arg(filePath));
}

void MainWindow::performSearch()
{
    QString query = ui->searchEdit->text();
    
    m_currentPage = 0;
    
    if (m_fileList.isEmpty()) {
        ui->infoLabel->setText("No files indexed yet. Please select a directory first.");
        m_allResults.clear();
        updatePaginationControls();
        displayCurrentPage();
        return;
    }
    
    m_allResults = m_fuzzyMatcher.search(query, 10000); // Large number to get most matches
    
    applyFilter();         // Apply extension filter
    applyFileTypeFilter(); // Apply file/directory filter
    
    calculateTotalPages();
    
    updatePaginationControls();
    displayCurrentPage();
    
    if (m_filteredResults.isEmpty() && !query.isEmpty()) {
        ui->infoLabel->setText(QString("No files found matching '%1'").arg(query));
    } else if (m_filteredResults.isEmpty() && query.isEmpty() && !m_fileList.isEmpty()) {
        ui->infoLabel->setText(QString("Directory: %1\nFound %2 files in total. Enter a search term.")
                             .arg(QDir(m_currentDir).dirName())
                             .arg(m_fileList.size()));
    }
    
    if (!query.isEmpty()) {
        addToSearchHistory(query);
    }
    
    onSearchFinished();
}

void MainWindow::updateResults(const QStringList &results)
{
    m_allResults = results;
    
    applyFilter();         // Apply extension filter
    applyFileTypeFilter(); // Apply file/directory filter
    
    m_currentPage = 0;
    
    calculateTotalPages();
    
    updatePaginationControls();
    displayCurrentPage();
}

void MainWindow::onNextClicked()
{
    if (m_currentPage < m_totalPages - 1) {
        m_currentPage++;
        displayCurrentPage();
        updatePaginationControls();
    }
}

void MainWindow::onPrevClicked()
{
    if (m_currentPage > 0) {
        m_currentPage--;
        displayCurrentPage();
        updatePaginationControls();
    }
}

void MainWindow::updatePaginationControls()
{
    if (m_filteredResults.isEmpty()) {
        if (m_fileList.isEmpty()) {
            ui->pageInfoLabel->setText("No files indexed yet");
        } else {
            ui->pageInfoLabel->setText("No matching files");
        }
        
        ui->prevButton->setEnabled(false);
        ui->nextButton->setEnabled(false);
        return;
    } 
    
    ui->pageInfoLabel->setText(QString("Page: %1/%2 (%3 files)")
                             .arg(m_totalPages > 0 ? m_currentPage + 1 : 0)
                             .arg(m_totalPages)
                             .arg(m_filteredResults.size()));
    
    ui->prevButton->setEnabled(m_currentPage > 0);
    ui->nextButton->setEnabled(m_currentPage < m_totalPages - 1);
}

void MainWindow::displayCurrentPage()
{
    ui->resultsList->clear();
    
    if (m_filteredResults.isEmpty()) {
        if (m_fileList.isEmpty()) {
            QListWidgetItem *item = new QListWidgetItem("No files indexed. Click 'Browse...' to select a directory.");
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled); // Make it non-selectable
            ui->resultsList->addItem(item);
        } else {
            QListWidgetItem *item = new QListWidgetItem("No matching files found. Try a different search term or filter.");
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled); // Make it non-selectable
            ui->resultsList->addItem(item);
        }
        return;
    }
    
    int startIdx = m_currentPage * PAGE_SIZE;
    int endIdx = qMin(startIdx + PAGE_SIZE, m_filteredResults.size());
    
    QIcon fileIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    QIcon dirIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    
    for (int i = startIdx; i < endIdx; ++i) {
        QString filePath = m_filteredResults.at(i);
        QFileInfo fileInfo(filePath);
        QListWidgetItem *item = new QListWidgetItem(fileInfo.fileName());
        
        if (fileInfo.isDir()) {
            item->setIcon(dirIcon);
        } else {
            item->setIcon(fileIcon);
        }
        
        item->setData(Qt::UserRole, filePath);
        
        if (!m_currentDir.isEmpty()) {
            QString relativePath = filePath;
            if (relativePath.startsWith(m_currentDir)) {
                relativePath.remove(0, m_currentDir.length() + 1); // +1 for the slash
            }
            item->setToolTip(relativePath);
        } else {
            item->setToolTip(filePath);
        }
        
        ui->resultsList->addItem(item);
    }
}

void MainWindow::onSearchFinished()
{
    saveSettings();
}

void MainWindow::loadSettings()
{
    m_currentDir = m_settings.value("lastDirectory", "").toString();
    
    m_searchHistory = m_settings.value("searchHistory").toStringList();
    
    m_bookmarks = m_settings.value("bookmarks").toStringList();
    updateBookmarks();
    
    m_isDarkTheme = m_settings.value("darkTheme", false).toBool();
    ui->actionDarkTheme->setChecked(m_isDarkTheme);
    applyTheme();
    
    m_previewEnabled = m_settings.value("previewEnabled", true).toBool();
    ui->actionShowPreview->setChecked(m_previewEnabled);
    ui->previewTextEdit->setVisible(m_previewEnabled);
    
    QString patterns = m_settings.value("ignorePatterns", "node_modules,.git,.svn,*.tmp").toString();
    ui->ignorePatternEdit->setText(patterns);
    m_ignorePatterns = patterns.split(",", Qt::SkipEmptyParts);
    
    m_showFiles = m_settings.value("showFiles", true).toBool();
    m_showDirectories = m_settings.value("showDirectories", false).toBool();
    ui->showFilesCheckbox->setChecked(m_showFiles);
    ui->showDirsCheckbox->setChecked(m_showDirectories);
    
    updateCompleter();
}

void MainWindow::saveSettings()
{
    if (!m_currentDir.isEmpty()) {
        m_settings.setValue("lastDirectory", m_currentDir);
    }
    
    m_settings.setValue("searchHistory", m_searchHistory);
    
    m_settings.setValue("bookmarks", m_bookmarks);
    
    m_settings.setValue("darkTheme", m_isDarkTheme);
    
    m_settings.setValue("previewEnabled", m_previewEnabled);
    
    m_settings.setValue("ignorePatterns", ui->ignorePatternEdit->text());
    
    m_settings.setValue("showFiles", m_showFiles);
    m_settings.setValue("showDirectories", m_showDirectories);
    
    m_settings.sync();
}

void MainWindow::addToSearchHistory(const QString &searchTerm)
{
    if (searchTerm.trimmed().isEmpty()) {
        return;
    }
    
    m_searchHistory.removeAll(searchTerm);
    
    m_searchHistory.prepend(searchTerm);
    
    while (m_searchHistory.size() > MAX_HISTORY_ITEMS) {
        m_searchHistory.removeLast();
    }
    
    updateCompleter();
}

void MainWindow::updateCompleter()
{
    m_historyModel->setStringList(m_searchHistory);
}


void MainWindow::setupFileTypeFilter()
{
    disconnect(ui->fileTypeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
               this, &MainWindow::onFilterByTypeChanged);
    
    ui->fileTypeFilter->clear();
    ui->fileTypeFilter->addItem("All Types");
    
    ui->fileTypeFilter->addItems(m_fileExtensions);
    
    connect(ui->fileTypeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onFilterByTypeChanged);
}

void MainWindow::setupBookmarks()
{
    connect(ui->bookmarksCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onBookmarkSelected);
    connect(ui->addBookmarkButton, &QPushButton::clicked,
            this, &MainWindow::onAddBookmark);
    connect(ui->removeBookmarkButton, &QPushButton::clicked,
            this, &MainWindow::onRemoveBookmark);
    
    updateBookmarks();
}

void MainWindow::setupThemeSupport()
{
    connect(ui->actionDarkTheme, &QAction::toggled,
            this, &MainWindow::onDarkThemeToggled);
}

void MainWindow::onFilterByTypeChanged(int index)
{
    if (index == 0) {
        m_currentFilter = "";
    } else if (index > 0 && index <= m_fileExtensions.size()) {
        m_currentFilter = m_fileExtensions.at(index - 1);
    }
    
    applyFilter();
    updatePaginationControls();
    displayCurrentPage();
}

void MainWindow::applyFilter()
{
    if (m_currentFilter.isEmpty()) {
        m_filteredResults = m_allResults;
        return;
    }
    
    m_filteredResults.clear();
    for (const QString &filePath : m_allResults) {
        QFileInfo fileInfo(filePath);
        QString suffix = fileInfo.suffix().toLower();
        
        if (suffix == m_currentFilter.toLower()) {
            m_filteredResults.append(filePath);
        }
    }
}

void MainWindow::onDarkThemeToggled(bool checked)
{
    m_isDarkTheme = checked;
    applyTheme();
    saveSettings();
}

void MainWindow::applyTheme()
{
    if (m_isDarkTheme) {
        QPalette darkPalette;
        QColor darkColor(45, 45, 45);
        QColor textColor(210, 210, 210);
        
        darkPalette.setColor(QPalette::Window, darkColor);
        darkPalette.setColor(QPalette::WindowText, textColor);
        darkPalette.setColor(QPalette::Base, QColor(18, 18, 18));
        darkPalette.setColor(QPalette::AlternateBase, darkColor);
        darkPalette.setColor(QPalette::ToolTipBase, darkColor);
        darkPalette.setColor(QPalette::ToolTipText, textColor);
        darkPalette.setColor(QPalette::Text, textColor);
        darkPalette.setColor(QPalette::Button, darkColor);
        darkPalette.setColor(QPalette::ButtonText, textColor);
        darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::HighlightedText, Qt::black);
        
        darkPalette.setColor(QPalette::Active, QPalette::Button, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
        darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(127, 127, 127));
        darkPalette.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
        darkPalette.setColor(QPalette::Disabled, QPalette::Light, QColor(53, 53, 53));
        
        qApp->setPalette(darkPalette);
        
        qApp->setStyleSheet(
            "QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; }"
            "QMenu { background-color: #333333; border: 1px solid #000000; }"
            "QMenu::item { background-color: transparent; }"
            "QMenu::item:selected { background-color: #2a82da; }"
        );
    } else {
        qApp->setPalette(style()->standardPalette());
        qApp->setStyleSheet("");
    }
}

void MainWindow::onAddBookmark()
{
    if (m_currentDir.isEmpty()) {
        QMessageBox::information(this, "Add Bookmark", "Please select a directory first.");
        return;
    }
    
    bool ok;
    QString bookmarkName = QInputDialog::getText(this, "Add Bookmark",
                                               "Enter a name for this bookmark:",
                                               QLineEdit::Normal,
                                               QDir(m_currentDir).dirName(), &ok);
    
    if (ok && !bookmarkName.isEmpty()) {
        QString bookmark = bookmarkName + "|" + m_currentDir;
        
        for (int i = 0; i < m_bookmarks.size(); ++i) {
            if (m_bookmarks[i].split("|").at(1) == m_currentDir) {
                QMessageBox::StandardButton reply = QMessageBox::question(this, "Replace Bookmark",
                    "A bookmark for this directory already exists. Do you want to replace it?",
                    QMessageBox::Yes | QMessageBox::No);
                
                if (reply == QMessageBox::Yes) {
                    m_bookmarks.removeAt(i);
                    break;
                } else {
                    return; // Cancel adding
                }
            }
        }
        
        m_bookmarks.append(bookmark);
        updateBookmarks();
        saveSettings();
    }
}

void MainWindow::onRemoveBookmark()
{
    int index = ui->bookmarksCombo->currentIndex();
    if (index > 0 && index <= m_bookmarks.size()) {
        m_bookmarks.removeAt(index - 1);
        updateBookmarks();
        saveSettings();
    }
}

void MainWindow::onBookmarkSelected(int index)
{
    if (index <= 0 || index > m_bookmarks.size()) {
        return;
    }
    
    QString path = m_bookmarks.at(index - 1).split("|").at(1);
    
    if (!path.isEmpty() && QDir(path).exists()) {
        m_currentDir = path;
        
        ui->searchEdit->clear();
        ui->resultsList->clear();
        
        QDir topDir(path);
        QStringList topLevelFiles = topDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        int dirCount = topDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).count();
        
        ui->infoLabel->setText(QString("Directory: %1\nContains %2 files and %3 subdirectories at top level")
                              .arg(QDir(path).dirName())
                              .arg(topLevelFiles.count())
                              .arg(dirCount));
        
        statusBar()->showMessage(QString("Scanning %1...").arg(path));
        
        ui->browseButton->setEnabled(false);
        ui->searchEdit->setEnabled(false);
        
        createProgressDialog();
        
        QFuture<QStringList> future = QtConcurrent::run(m_directoryScanner, &DirectoryScanner::scanDirectory, path);
        m_scanWatcher->setFuture(future);
    } else {
        QMessageBox::warning(this, "Invalid Bookmark", 
                            "The directory for this bookmark no longer exists. The bookmark will be removed.");
        m_bookmarks.removeAt(index - 1);
        updateBookmarks();
        saveSettings();
    }
}

void MainWindow::updateBookmarks()
{
    disconnect(ui->bookmarksCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
               this, &MainWindow::onBookmarkSelected);
    
    ui->bookmarksCombo->clear();
    ui->bookmarksCombo->addItem("Select Bookmark");
    
    for (const QString &bookmark : m_bookmarks) {
        QStringList parts = bookmark.split("|");
        if (parts.size() >= 2) {
            ui->bookmarksCombo->addItem(parts.at(0));
        }
    }
    
    connect(ui->bookmarksCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onBookmarkSelected);
}

QStringList MainWindow::getFileTypeExtensions(const QStringList &files)
{
    QSet<QString> extensions;
    
    for (const QString &filePath : files) {
        QFileInfo fileInfo(filePath);
        QString suffix = fileInfo.suffix().toLower();
        if (!suffix.isEmpty()) {
            extensions.insert(suffix);
        }
    }
    
    QStringList extensionList = extensions.values();
    std::sort(extensionList.begin(), extensionList.end());
    
    return extensionList;
}

void MainWindow::setupContextMenu()
{
    m_contextMenu = new QMenu(this);
    
    m_openAction = new QAction("Open File", this);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openSelectedFile);
    
    m_openFolderAction = new QAction("Open Containing Folder", this);
    connect(m_openFolderAction, &QAction::triggered, this, &MainWindow::openContainingFolder);
    
    m_copyPathAction = new QAction("Copy Full Path", this);
    connect(m_copyPathAction, &QAction::triggered, this, &MainWindow::copyFullPath);
    
    m_copyNameAction = new QAction("Copy File Name", this);
    connect(m_copyNameAction, &QAction::triggered, this, &MainWindow::copyFileName);
    
    m_copyRelativePathAction = new QAction("Copy Relative Path", this);
    connect(m_copyRelativePathAction, &QAction::triggered, this, &MainWindow::copyRelativePath);
    
    m_contextMenu->addAction(m_openAction);
    m_contextMenu->addAction(m_openFolderAction);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_copyPathAction);
    m_contextMenu->addAction(m_copyNameAction);
    m_contextMenu->addAction(m_copyRelativePathAction);
    
    ui->resultsList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->resultsList, &QListWidget::customContextMenuRequested, 
            this, &MainWindow::showContextMenu);
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = ui->resultsList->itemAt(pos);
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) {
        return;
    }
    
    m_contextMenu->exec(ui->resultsList->mapToGlobal(pos));
}

void MainWindow::openSelectedFile()
{
    QString filePath = getSelectedFilePath();
    if (!filePath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        statusBar()->showMessage(QString("Opening file: %1").arg(filePath), 3000);
    }
}

void MainWindow::openContainingFolder()
{
    QString filePath = getSelectedFilePath();
    if (!filePath.isEmpty()) {
        QFileInfo fileInfo(filePath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absolutePath()));
        statusBar()->showMessage(QString("Opening folder: %1").arg(fileInfo.absolutePath()), 3000);
    }
}

void MainWindow::copyFullPath()
{
    QString filePath = getSelectedFilePath();
    if (!filePath.isEmpty()) {
        QApplication::clipboard()->setText(filePath);
        statusBar()->showMessage(QString("Full path copied to clipboard: %1").arg(filePath), 3000);
    }
}

void MainWindow::copyFileName()
{
    QString filePath = getSelectedFilePath();
    if (!filePath.isEmpty()) {
        QFileInfo fileInfo(filePath);
        QString fileName = fileInfo.fileName();
        QApplication::clipboard()->setText(fileName);
        statusBar()->showMessage(QString("File name copied to clipboard: %1").arg(fileName), 3000);
    }
}

void MainWindow::copyRelativePath()
{
    QString filePath = getSelectedFilePath();
    if (!filePath.isEmpty() && !m_currentDir.isEmpty()) {
        QString relativePath = filePath;
        if (relativePath.startsWith(m_currentDir)) {
            relativePath.remove(0, m_currentDir.length() + 1);  // +1 for the slash
        }
        QApplication::clipboard()->setText(relativePath);
        statusBar()->showMessage(QString("Relative path copied to clipboard: %1").arg(relativePath), 3000);
    }
}

void MainWindow::setupPreviewPane()
{
    connect(ui->actionShowPreview, &QAction::toggled, this, &MainWindow::togglePreviewPane);
    
    QList<int> sizes;
    sizes << (width() * 7 / 10) << (width() * 3 / 10);
    ui->mainSplitter->setSizes(sizes);
}

void MainWindow::togglePreviewPane(bool checked)
{
    m_previewEnabled = checked;
    ui->previewTextEdit->setVisible(checked);
    
    if (checked) {
        previewSelectedFile();
    }
    
    m_settings.setValue("previewEnabled", m_previewEnabled);
}

void MainWindow::previewSelectedFile()
{
    if (!m_previewEnabled) {
        return;
    }
    
    QString filePath = getSelectedFilePath();
    if (filePath.isEmpty()) {
        ui->previewTextEdit->clear();
        ui->previewTextEdit->setPlaceholderText("File preview will appear here");
        return;
    }
    
    if (isFileTypeText(filePath)) {
        QString previewContent = getFilePreview(filePath);
        ui->previewTextEdit->setPlainText(previewContent);
    } else {
        QFileInfo fileInfo(filePath);
        ui->previewTextEdit->clear();
        ui->previewTextEdit->append(QString("File: %1").arg(fileInfo.fileName()));
        ui->previewTextEdit->append(QString("Type: %1").arg(fileInfo.suffix().toUpper()));
        ui->previewTextEdit->append(QString("Size: %1 bytes").arg(fileInfo.size()));
        ui->previewTextEdit->append(QString("Modified: %1").arg(fileInfo.lastModified().toString()));
        ui->previewTextEdit->append("\nPreview not available for this file type.");
    }
}

void MainWindow::setupKeyboardShortcuts()
{
    m_upShortcut = new QShortcut(QKeySequence(Qt::Key_Up), this);
    connect(m_upShortcut, &QShortcut::activated, this, &MainWindow::handleKeyUp);
    
    m_downShortcut = new QShortcut(QKeySequence(Qt::Key_Down), this);
    connect(m_downShortcut, &QShortcut::activated, this, &MainWindow::handleKeyDown);
    
    m_enterShortcut = new QShortcut(QKeySequence(Qt::Key_Return), this);
    connect(m_enterShortcut, &QShortcut::activated, this, &MainWindow::handleKeyEnter);
    
    m_escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(m_escShortcut, &QShortcut::activated, this, &MainWindow::handleKeyEscape);
}

void MainWindow::handleKeyUp()
{
    if (!ui->resultsList->hasFocus()) {
        int row = ui->resultsList->currentRow();
        if (row > 0) {
            ui->resultsList->setCurrentRow(row - 1);
        }
    }
}

void MainWindow::handleKeyDown()
{
    if (!ui->resultsList->hasFocus()) {
        int row = ui->resultsList->currentRow();
        if (row < ui->resultsList->count() - 1) {
            ui->resultsList->setCurrentRow(row + 1);
        }
    }
}

void MainWindow::handleKeyEnter()
{
    if (!ui->resultsList->hasFocus() && !ui->searchEdit->hasFocus()) {
        openSelectedFile();
    }
}

void MainWindow::handleKeyEscape()
{
    if (!ui->searchEdit->text().isEmpty()) {
        ui->searchEdit->clear();
    } else {
        ui->searchEdit->setFocus();
    }
}

void MainWindow::setupIgnorePatterns()
{
    if (ui->ignorePatternEdit->text().isEmpty()) {
        ui->ignorePatternEdit->setText("node_modules,.git,.svn,*.tmp");
    }
}

void MainWindow::onIgnorePatternChanged()
{
    QString patterns = ui->ignorePatternEdit->text();
    m_ignorePatterns = patterns.split(",", Qt::SkipEmptyParts);
    
    if (!m_currentDir.isEmpty() && !m_fileList.isEmpty()) {
        statusBar()->showMessage("Applying new ignore patterns...", 2000);
        
        QFuture<QStringList> future = QtConcurrent::run(m_directoryScanner, &DirectoryScanner::scanDirectory, m_currentDir);
        m_scanWatcher->setFuture(future);
    }
}

bool MainWindow::shouldIgnoreFile(const QString &filePath) const
{
    if (m_ignorePatterns.isEmpty()) {
        return false;
    }
    
    QFileInfo fileInfo(filePath);
    QString fileName = fileInfo.fileName();
    QString relativePath = filePath;
    
    if (relativePath.startsWith(m_currentDir)) {
        relativePath.remove(0, m_currentDir.length() + 1);  // +1 for the slash
    }
    
    for (const QString &pattern : m_ignorePatterns) {
        QString trimmedPattern = pattern.trimmed();
        
        if (trimmedPattern.startsWith("*")) {
            QString ext = trimmedPattern.mid(1);  // Remove the asterisk
            if (fileName.endsWith(ext)) {
                return true;
            }
        } else if (relativePath.contains(trimmedPattern)) {
            return true;
        }
    }
    
    return false;
}

QString MainWindow::getSelectedFilePath() const
{
    QListWidgetItem *item = ui->resultsList->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) {
        return QString();
    }
    
    return item->data(Qt::UserRole).toString();
}

QString MainWindow::getFilePreview(const QString &filePath, int maxLines) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString("Cannot open file for preview");
    }
    
    QTextStream in(&file);
    QString content;
    int lineCount = 0;
    bool truncated = false;
    
    while (!in.atEnd() && lineCount < maxLines) {
        content.append(in.readLine() + "\n");
        lineCount++;
    }
    
    if (!in.atEnd()) {
        truncated = true;
    }
    
    file.close();
    
    if (truncated) {
        if (content.endsWith('\n')) {
            content.chop(1);
        }
        content.append(QString("\n\n[Preview truncated, showing first %1 lines]").arg(maxLines));
    }
    
    return content;
}

bool MainWindow::isFileTypeText(const QString &filePath) const
{
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(filePath);
    QString mimeStr = mime.name();
    
    if (mimeStr.startsWith("text/")) {
        return true;
    }
    
    static QStringList textTypes = {
        "application/json",
        "application/xml",
        "application/javascript",
        "application/x-yaml",
        "application/x-shellscript",
        "application/x-perl",
        "application/x-ruby",
        "application/x-python"
    };
    
    if (textTypes.contains(mimeStr)) {
        return true;
    }
    
    static QStringList sourceExtensions = {
        "c", "cpp", "h", "hpp", "cs", "java", "py", "rb", "js", "ts", "php",
        "html", "htm", "css", "scss", "sass", "less", "xml", "json", "yml", "yaml",
        "md", "markdown", "txt", "sh", "bat", "ps1", "cmake", "sql"
    };
    
    QFileInfo fileInfo(filePath);
    QString suffix = fileInfo.suffix().toLower();
    
    return sourceExtensions.contains(suffix);
}


void MainWindow::setupFileTypeCheckboxes()
{
    connect(ui->showFilesCheckbox, &QCheckBox::toggled, this, &MainWindow::onShowFilesToggled);
    connect(ui->showDirsCheckbox, &QCheckBox::toggled, this, &MainWindow::onShowDirectoriesToggled);
    
    m_showFilesCheckbox = ui->showFilesCheckbox;
    m_showDirsCheckbox = ui->showDirsCheckbox;
    
    m_showFiles = m_showFilesCheckbox->isChecked();
    m_showDirectories = m_showDirsCheckbox->isChecked();
}

void MainWindow::onShowFilesToggled(bool checked)
{
    m_showFiles = checked;
    
    if (!m_showFiles && !m_showDirectories) {
        m_showDirsCheckbox->setChecked(true);
    }
    
    applyFileTypeFilter();
    updatePaginationControls();
    displayCurrentPage();
    
    saveSettings();
}

void MainWindow::onShowDirectoriesToggled(bool checked)
{
    m_showDirectories = checked;
    
    if (!m_showFiles && !m_showDirectories) {
        m_showFilesCheckbox->setChecked(true);
    }
    
    applyFileTypeFilter();
    updatePaginationControls();
    displayCurrentPage();
    
    saveSettings();
}

void MainWindow::applyFileTypeFilter()
{
    if ((m_showFiles && m_showDirectories) || (!m_showFiles && !m_showDirectories)) {
        return;
    }
    
    QStringList tempResults = m_filteredResults;
    m_filteredResults.clear();
    
    for (const QString &path : tempResults) {
        bool isDir = isDirectory(path);
        
        if ((isDir && m_showDirectories) || (!isDir && m_showFiles)) {
            m_filteredResults.append(path);
        }
    }
    
    calculateTotalPages();
}

void MainWindow::calculateTotalPages()
{
    m_totalPages = m_filteredResults.isEmpty() ? 1 : (m_filteredResults.size() + PAGE_SIZE - 1) / PAGE_SIZE;
}

bool MainWindow::isDirectory(const QString &path) const
{
    QFileInfo fileInfo(path);
    return fileInfo.isDir();
} 