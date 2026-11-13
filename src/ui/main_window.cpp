#include "ui/main_window.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardItem>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_central = new QWidget(this);
    m_mainLayout = new QVBoxLayout(m_central);
    m_mainLayout->setContentsMargins(6, 6, 6, 6);
    m_mainLayout->setSpacing(6);

    connect(&m_sshClient, &SSHClient::connected, this, &MainWindow::onRemoteConnected);
    connect(&m_sshClient, &SSHClient::errorOccurred, this, [this](const QString &message) {
        appendTerminalText(QString("[ERROR] %1\n").arg(message));
    });

    buildTerminalPanel();
    buildFileTree();
    buildEditorArea();

    setCentralWidget(m_central);
    setWindowTitle("LightSSH IDE");
    statusBar()->showMessage("Open a terminal tab for local shell access");

    if (m_terminal) {
        m_terminal->appendTerminalText("Welcome to LightSSH IDE\n");
        m_terminal->appendTerminalText("Example local shell command: dir\n");
    }
}

void MainWindow::onConnectClicked()
{
    addTerminalTab();
}

void MainWindow::onSaveClicked()
{
    if (m_editorTabs->count() == 0) {
        return;
    }

    const int currentIndex = m_editorTabs->currentIndex();
    auto *page = qobject_cast<QWidget *>(m_editorTabs->widget(currentIndex));
    if (!page) {
        return;
    }

    auto *editor = page->findChild<CodeEditor *>("remote_editor");
    if (!editor) {
        return;
    }

    const QString currentPath = editor->filePath();
    if (currentPath.isEmpty()) {
        return;
    }

    const bool ok = m_sshClient.writeRemoteFile(currentPath, editor->currentText());
    if (ok) {
        statusBar()->showMessage(QString("Saved %1").arg(currentPath));
    } else {
        statusBar()->showMessage(QString("Save failed: %1").arg(m_sshClient.lastError()));
    }
}

void MainWindow::onRemoteConnected()
{
    if (!m_sshClient.isConnected()) {
        return;
    }

    const QList<RemoteEntry> entries = m_sshClient.listRemoteDirectory(m_currentRemoteRoot);
    populateRemoteTree(m_currentRemoteRoot, entries);
    statusBar()->showMessage(QString("Connected. Remote root: %1").arg(m_currentRemoteRoot));
    if (m_terminal) {
        appendTerminalText(QString("Connected to %1\n").arg(m_currentRemoteRoot));
    }
}

void MainWindow::onTreeActivated(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    const QVariant data = m_fileModel->data(index, Qt::UserRole);
    if (!data.isValid()) {
        return;
    }

    const QString path = data.toString();
    if (path.isEmpty()) {
        return;
    }

    const QString content = m_sshClient.readRemoteFile(path);
    openRemoteFile(path, content);
}

void MainWindow::onTabCloseRequested(int index)
{
    if (index < 0 || index >= m_editorTabs->count()) {
        return;
    }

    m_editorTabs->removeTab(index);
}

void MainWindow::appendTerminalText(const QString &text)
{
    if (m_terminal) {
        m_terminal->appendTerminalText(text);
    }
}

void MainWindow::buildTerminalPanel()
{
    m_terminalTabs = new QTabWidget(this);
    m_terminalTabs->setTabsClosable(true);
    m_terminalTabs->setMovable(true);
    m_terminalTabs->setDocumentMode(true);
    connect(m_terminalTabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onTerminalTabCloseRequested);

    m_connectButton = new QPushButton("New Terminal", this);
    m_saveButton = new QPushButton("Save", this);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_saveButton, &QPushButton::clicked, this, &MainWindow::onSaveClicked);

    auto *buttonRow = new QWidget(this);
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addWidget(m_connectButton);
    buttonLayout->addWidget(m_saveButton);
    buttonLayout->addStretch();

    auto *terminalContainer = new QWidget(this);
    auto *terminalLayout = new QVBoxLayout(terminalContainer);
    terminalLayout->setContentsMargins(0, 0, 0, 0);
    terminalLayout->addWidget(m_terminalTabs);
    terminalLayout->addWidget(buttonRow);

    addTerminalTab();
    m_mainLayout->addWidget(terminalContainer);
}

void MainWindow::addTerminalTab()
{
    auto *terminal = new TerminalWidget(this);
    const int index = m_terminalTabs->addTab(terminal, QString("Terminal %1").arg(m_terminalTabs->count() + 1));
    m_terminalTabs->setCurrentIndex(index);
    m_terminal = terminal;
}

void MainWindow::onTerminalTabCloseRequested(int index)
{
    if (index < 0 || index >= m_terminalTabs->count()) {
        return;
    }

    auto *terminal = qobject_cast<TerminalWidget *>(m_terminalTabs->widget(index));
    if (terminal) {
        terminal->closeTerminal();
        m_terminalTabs->removeTab(index);
        delete terminal;
    }
}

void MainWindow::buildFileTree()
{
    m_mainSplitter = new QSplitter(Qt::Vertical, this);
    m_mainSplitter->setChildrenCollapsible(false);
    m_mainSplitter->setHandleWidth(6);

    auto *workspaceSplitter = new QSplitter(Qt::Horizontal, this);
    workspaceSplitter->setChildrenCollapsible(false);
    workspaceSplitter->setHandleWidth(6);

    m_treeView = new QTreeView(this);
    m_fileModel = new QStandardItemModel(this);

    m_fileModel->setHorizontalHeaderLabels({QStringLiteral("Remote Files")});
    m_treeView->setModel(m_fileModel);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setRootIsDecorated(true);

    connect(m_treeView, &QTreeView::doubleClicked, this, &MainWindow::onTreeActivated);

    workspaceSplitter->addWidget(m_treeView);
    workspaceSplitter->setStretchFactor(0, 1);
    m_mainSplitter->addWidget(workspaceSplitter);
    m_mainSplitter->setStretchFactor(0, 1);

    m_mainLayout->addWidget(m_mainSplitter);
}

void MainWindow::buildEditorArea()
{
    m_workbenchSplitter = new QSplitter(Qt::Vertical, this);
    m_editorTabs = new QTabWidget(this);
    m_editorTabs->setMovable(true);
    m_editorTabs->setTabsClosable(true);
    m_editorTabs->setDocumentMode(true);
    connect(m_editorTabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);

    auto *demoPage = new QWidget(this);
    auto *demoLayout = new QVBoxLayout(demoPage);
    auto *editor = new CodeEditor(demoPage);
    editor->setObjectName("remote_editor");
    editor->setFilePath("/home/test_lontoone/project/main.cpp");
    editor->loadText("#include <iostream>\n\nint main() {\n    std::cout << \"LightSSH IDE ready\\n\";\n    return 0;\n}\n");
    demoLayout->addWidget(editor);
    m_editorTabs->addTab(demoPage, "main.cpp");

    m_workbenchSplitter->addWidget(m_editorTabs);
    m_workbenchSplitter->setStretchFactor(0, 3);

    auto *workspacePanel = m_mainSplitter->widget(0);
    if (workspacePanel) {
        auto *currentH = qobject_cast<QSplitter *>(workspacePanel);
        if (currentH) {
            currentH->addWidget(m_workbenchSplitter);
            currentH->setStretchFactor(1, 3);
        }
    }
}

void MainWindow::populateRemoteTree(const QString &rootPath, const QList<RemoteEntry> &entries)
{
    m_fileModel->clear();
    m_fileModel->setHorizontalHeaderLabels({QStringLiteral("Remote Files")});

    auto *rootItem = new QStandardItem(rootPath);
    rootItem->setEditable(false);
    rootItem->setData(rootPath, Qt::UserRole);

    for (const RemoteEntry &entry : entries) {
        auto *child = new QStandardItem(entry.name);
        child->setEditable(false);
        child->setData(entry.path, Qt::UserRole);
        if (entry.isDir) {
            child->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
        } else {
            child->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
        }
        rootItem->appendRow(child);
    }

    m_fileModel->appendRow(rootItem);
    m_treeView->expandAll();
}

void MainWindow::openRemoteFile(const QString &path, const QString &text)
{
    const QString title = QFileInfo(path).fileName();
    for (int i = 0; i < m_editorTabs->count(); ++i) {
        auto *page = qobject_cast<QWidget *>(m_editorTabs->widget(i));
        if (!page) {
            continue;
        }
        auto *editor = page->findChild<CodeEditor *>("remote_editor");
        if (editor && editor->filePath() == path) {
            m_editorTabs->setCurrentIndex(i);
            return;
        }
    }

    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *editor = new CodeEditor(page);
    editor->setObjectName("remote_editor");
    editor->setFilePath(path);
    editor->loadText(text.isEmpty() ? QString("# open file: %1\n").arg(path) : text);
    layout->addWidget(editor);
    m_editorTabs->addTab(page, title);
    m_editorTabs->setCurrentWidget(page);
}
