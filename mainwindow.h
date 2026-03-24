#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDateTime>
#include <QJsonObject>
#include <QMap>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStandardItemModel;
class QTableView;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);

private:
    struct Recording {
        int id = 0;
        QString agent;
        QString phone;
        QString transcript;
        QString filePath;
        QDateTime timestamp;
        int durationSec = 0;
        QString language;
    };

    struct FilterDef {
        QString id;
        QString type;
        QString label;
        QStringList options;
        QString defaultValue;
    };

    struct ColumnDef {
        QString key;
        QString title;
        int width = 120;
    };

    void setupWindow();
    void setupLayout();
    void applyModernStyle();
    void loadConfig();
    void seedSampleData();
    void buildDynamicFilters(QWidget *host);
    void buildDynamicTableColumns();
    void refreshResults();
    void updateDetailsPanel(int row);
    QString valueForColumn(const Recording &record, const QString &key) const;
    bool matchesFilters(const Recording &record, const QString &query) const;
    QString normalizeWord(const QString &value) const;

    QList<FilterDef> filterDefs;
    QList<ColumnDef> columnDefs;
    QList<Recording> allRecords;
    QList<int> visibleRecordIndexes;
    QJsonObject configRoot;

    QLineEdit *searchInput = nullptr;
    QLabel *resultsInfo = nullptr;
    QTableView *resultsTable = nullptr;
    QStandardItemModel *tableModel = nullptr;
    QListWidget *hitsList = nullptr;
    QLabel *metaInfo = nullptr;
    QPushButton *playButton = nullptr;
    QPushButton *openButton = nullptr;
    QMap<QString, QWidget*> filterWidgets;
};
#endif // MAINWINDOW_H
