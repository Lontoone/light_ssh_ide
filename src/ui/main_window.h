#pragma once

#include <QMainWindow>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTreeView>
#include <QVBoxLayout>

#include "core/ssh_client.h"
#include "ui/code_editor.h"
#include "ui/terminal_widget.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onConnectClicked();
    void onSaveClicked();
    void onRemoteConnected();
    void onTreeActivated(const QModelIndex &index);
    void onTabCloseRequested(int index);
    void onTerminalTabCloseRequested(int index);

private:
    void buildTerminalPanel();
    void addTerminalTab();
    void buildFileTree();
    void buildEditorArea();
    void populateRemoteTree(const QString &rootPath, const QList<RemoteEntry> &entries = QList<RemoteEntry>());
    void openRemoteFile(const QString &path, const QString &text = QString());
    void appendTerminalText(const QString &text);

    QWidget *m_central = nullptr;
    QVBoxLayout *m_mainLayout = nullptr;

    QSplitter *m_mainSplitter = nullptr;
    QSplitter *m_workbenchSplitter = nullptr;

    QTreeView *m_treeView = nullptr;
    QStandardItemModel *m_fileModel = nullptr;

    QTabWidget *m_terminalTabs = nullptr;
    TerminalWidget *m_terminal = nullptr;
    QTabWidget *m_editorTabs = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_saveButton = nullptr;

    QString m_currentRemoteRoot = "/home/test_lontoone";
    SSHClient m_sshClient;
};
