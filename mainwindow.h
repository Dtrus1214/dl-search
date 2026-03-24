#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDateTime>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QMap>
#include <QUrl>
#include "searchengine.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QMediaPlayer;
class QAudioOutput;
class QPushButton;
class QStandardItemModel;
class QTableView;
class QTimer;
class QWidget;
class WaveformView;

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
        struct TimedWord {
            QString text;
            double startSec = 0.0;
            double endSec = 0.0;
        };
        QList<TimedWord> words;
    };

    struct KeywordHit {
        QString keyword;
        double startSec = 0.0;
        double endSec = 0.0;
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
    void setupPlayback();
    void loadConfig();
    void seedSampleData();
    void loadAudioFiles();
    void processNextTranscription();
    void onTranscriptionFinished();
    void buildDynamicFilters(QWidget *host);
    void buildDynamicTableColumns();
    void refreshResults();
    void updateDetailsPanel(int row);
    void playSelectedSnippet();
    void openFullAudio();
    void stopSnippetPlayback();
    bool ensureTranscriptForRecord(Recording &record);
    QString normalizeToken(const QString &value) const;
    QString loadTranscriptForAudio(const QString &audioPath) const;
    int estimateDurationSecFromWav(const QString &audioPath) const;
    QList<QString> parseKeywords(const QString &value) const;
    QList<KeywordHit> detectKeywordHits(const Recording &record, const QList<QString> &keywords) const;
    static QString formatSec(double sec);
    QString valueForColumn(const Recording &record, const QString &key) const;
    bool matchesFilters(const Recording &record, const QList<QString> &keywords) const;
    QString normalizeWord(const QString &value) const;

    QList<FilterDef> filterDefs;
    QList<ColumnDef> columnDefs;
    QList<Recording> allRecords;
    QList<QString> pendingTranscriptionFiles;
    QList<int> visibleRecordIndexes;
    QJsonObject configRoot;

    QLineEdit *searchInput = nullptr;
    QLabel *resultsInfo = nullptr;
    QLabel *transcriptionStatus = nullptr;
    QProgressBar *transcriptionProgress = nullptr;
    QTableView *resultsTable = nullptr;
    QStandardItemModel *tableModel = nullptr;
    QListWidget *hitsList = nullptr;
    QLabel *metaInfo = nullptr;
    QPushButton *playButton = nullptr;
    QPushButton *openButton = nullptr;
    WaveformView *waveformView = nullptr;
    QMap<QString, QWidget*> filterWidgets;
    QMediaPlayer *player = nullptr;
    QAudioOutput *audioOutput = nullptr;
    QTimer *snippetStopTimer = nullptr;
    SearchEngine searchEngine;
    QFutureWatcher<Recording> transcriptionWatcher;
    int transcriptionTotal = 0;
    int transcriptionProcessed = 0;
    int transcriptionFailed = 0;
    int selectedVisibleRow = -1;
    QList<KeywordHit> currentHits;
};
#endif // MAINWINDOW_H
