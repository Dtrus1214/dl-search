#include "mainwindow.h"
#include "waveformview.h"

#include <QtConcurrent>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QProgressDialog>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioOutput>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    loadConfig();
    seedSampleData();
    setupWindow();
    setupLayout();
    setupPlayback();
    connect(&transcriptionWatcher, &QFutureWatcher<Recording>::finished, this, &MainWindow::onTranscriptionFinished);
    applyModernStyle();
    refreshResults();
}

void MainWindow::setupWindow()
{
    setWindowTitle("Speech Search Console");
    resize(1380, 860);
}

void MainWindow::setupLayout()
{
    auto *root = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(20, 20, 20, 20);
    rootLayout->setSpacing(14);

    auto *title = new QLabel("Call Recording Search", root);
    title->setObjectName("TitleLabel");
    auto *subtitle = new QLabel("Keyword spotting and review across phone records", root);
    subtitle->setObjectName("SubtitleLabel");

    auto *searchRow = new QHBoxLayout();
    searchInput = new QLineEdit(root);
    searchInput->setPlaceholderText("Search keyword(s), comma separated (example: dispute, refund, contract)...");
    searchInput->setClearButtonEnabled(true);
    searchInput->setMinimumHeight(40);

    auto *loadButton = new QPushButton("Load Audio Files", root);
    loadButton->setMinimumHeight(40);
    auto *searchButton = new QPushButton("Search", root);
    searchButton->setMinimumHeight(40);

    resultsInfo = new QLabel("0 results", root);
    resultsInfo->setObjectName("MutedLabel");
    transcriptionStatus = new QLabel("Idle", root);
    transcriptionStatus->setObjectName("MutedLabel");
    transcriptionProgress = new QProgressBar(root);
    transcriptionProgress->setRange(0, 100);
    transcriptionProgress->setValue(0);
    transcriptionProgress->setTextVisible(true);
    transcriptionProgress->setFixedWidth(220);
    transcriptionProgress->setVisible(false);

    searchRow->addWidget(searchInput, 1);
    searchRow->addWidget(loadButton);
    searchRow->addWidget(searchButton);
    searchRow->addSpacing(8);
    searchRow->addWidget(resultsInfo);
    searchRow->addWidget(transcriptionStatus);
    searchRow->addWidget(transcriptionProgress);

    auto *split = new QSplitter(Qt::Horizontal, root);
    split->setChildrenCollapsible(false);

    auto *leftPane = new QWidget(split);
    auto *leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);

    auto *filtersGroup = new QGroupBox("Dynamic Filters", leftPane);
    auto *filtersLayout = new QVBoxLayout(filtersGroup);
    filtersLayout->setContentsMargins(12, 14, 12, 12);
    buildDynamicFilters(filtersGroup);

    auto *tableGroup = new QGroupBox("Search Results", leftPane);
    auto *tableLayout = new QVBoxLayout(tableGroup);
    tableLayout->setContentsMargins(0, 8, 0, 0);

    resultsTable = new QTableView(tableGroup);
    tableModel = new QStandardItemModel(this);
    resultsTable->setModel(tableModel);
    resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    resultsTable->setAlternatingRowColors(true);
    resultsTable->verticalHeader()->setVisible(false);
    resultsTable->horizontalHeader()->setStretchLastSection(true);
    resultsTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    resultsTable->setShowGrid(false);
    buildDynamicTableColumns();
    tableLayout->addWidget(resultsTable);

    leftLayout->addWidget(filtersGroup);
    leftLayout->addWidget(tableGroup, 1);

    auto *rightPane = new QWidget(split);
    auto *rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(12);

    auto *reviewGroup = new QGroupBox("Review Panel", rightPane);
    auto *reviewLayout = new QVBoxLayout(reviewGroup);
    reviewLayout->setContentsMargins(14, 14, 14, 14);
    reviewLayout->setSpacing(10);

    metaInfo = new QLabel("Select a recording to inspect metadata and keyword hits.", reviewGroup);
    metaInfo->setWordWrap(true);
    hitsList = new QListWidget(reviewGroup);
    hitsList->setMinimumHeight(180);
    waveformView = new WaveformView(reviewGroup);
    waveformView->setMinimumHeight(220);

    auto *actionRow = new QHBoxLayout();
    playButton = new QPushButton("Play Snippet", reviewGroup);
    openButton = new QPushButton("Open Full Audio", reviewGroup);
    playButton->setEnabled(false);
    openButton->setEnabled(false);
    actionRow->addWidget(playButton);
    actionRow->addWidget(openButton);

    reviewLayout->addWidget(metaInfo);
    reviewLayout->addWidget(waveformView);
    reviewLayout->addWidget(hitsList, 1);
    reviewLayout->addLayout(actionRow);

    rightLayout->addWidget(reviewGroup, 1);

    split->addWidget(leftPane);
    split->addWidget(rightPane);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    rootLayout->addWidget(title);
    rootLayout->addWidget(subtitle);
    rootLayout->addLayout(searchRow);
    rootLayout->addWidget(split, 1);
    setCentralWidget(root);

    connect(searchButton, &QPushButton::clicked, this, &MainWindow::refreshResults);
    connect(loadButton, &QPushButton::clicked, this, &MainWindow::loadAudioFiles);
    connect(searchInput, &QLineEdit::returnPressed, this, &MainWindow::refreshResults);
    connect(resultsTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &current, const QModelIndex &) {
        updateDetailsPanel(current.row());
    });
    connect(playButton, &QPushButton::clicked, this, &MainWindow::playSelectedSnippet);
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openFullAudio);
    connect(hitsList, &QListWidget::currentRowChanged, this,
            [this](int row) {
        if (row < 0 || row >= currentHits.size()) {
            return;
        }
        const KeywordHit &hit = currentHits.at(row);
        waveformView->setPlaybackPositionSec(hit.startSec);
    });
}

void MainWindow::setupPlayback()
{
    player = new QMediaPlayer(this);
    snippetStopTimer = new QTimer(this);
    snippetStopTimer->setSingleShot(true);
    connect(snippetStopTimer, &QTimer::timeout, this, &MainWindow::stopSnippetPlayback);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    audioOutput = new QAudioOutput(this);
    audioOutput->setVolume(0.8);
    player->setAudioOutput(audioOutput);
#endif

    connect(player, &QMediaPlayer::positionChanged, this,
            [this](qint64 posMs) {
        if (waveformView) {
            waveformView->setPlaybackPositionSec(static_cast<double>(posMs) / 1000.0);
        }
    });
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
        if (waveformView) {
            waveformView->setPlaybackActive(state == QMediaPlayer::PlayingState);
        }
    });
#else
    connect(player, &QMediaPlayer::stateChanged, this,
            [this](QMediaPlayer::State state) {
        if (waveformView) {
            waveformView->setPlaybackActive(state == QMediaPlayer::PlayingState);
        }
    });
#endif
}

void MainWindow::applyModernStyle()
{
    const QString style = QStringLiteral(
        "QMainWindow { background: #101827; }"
        "#TitleLabel { font-size: 24px; font-weight: 700; color: #e5e7eb; }"
        "#SubtitleLabel { color: #9ca3af; font-size: 13px; }"
        "#MutedLabel { color: #9ca3af; }"
        "QGroupBox { border: 1px solid #374151; border-radius: 10px; margin-top: 8px; color: #e5e7eb; font-weight: 600; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px 0 4px; }"
        "QLineEdit, QComboBox, QTableView, QListWidget { background: #111827; border: 1px solid #374151; border-radius: 8px; color: #e5e7eb; padding: 6px; }"
        "QLineEdit:focus, QComboBox:focus { border: 1px solid #3b82f6; }"
        "QLabel { color: #d1d5db; }"
        "QPushButton { background: #2563eb; color: #ffffff; border: none; border-radius: 8px; padding: 10px 14px; font-weight: 600; }"
        "QPushButton:hover { background: #1d4ed8; }"
        "QPushButton:disabled { background: #374151; color: #9ca3af; }"
        "QHeaderView::section { background: #1f2937; color: #d1d5db; border: none; padding: 8px; }"
        "QTableView::item:selected { background: #1e3a8a; color: #ffffff; }"
        "QListWidget::item:selected { background: #1e3a8a; color: #ffffff; }"
        "QProgressBar {"
        " background: #111827;"
        " border: 1px solid #374151;"
        " border-radius: 8px;"
        " color: #e5e7eb;"
        " text-align: center;"
        " height: 20px;"
        "}"
        "QProgressBar::chunk {"
        " background-color: #2563eb;"
        " border-radius: 8px;"
        "}"
        "QProgressBar:disabled {"
        " border: 1px solid #374151;"
        " color: #9ca3af;"
        "}"
    );
    qApp->setStyleSheet(style);
}

void MainWindow::loadConfig()
{
    QString configPath = QCoreApplication::applicationDirPath() + "/config/ui_config.json";
    if (!QFileInfo::exists(configPath)) {
        configPath = "config/ui_config.json";
    }

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return;
    }

    configRoot = doc.object();
    const QJsonArray filters = configRoot.value("filters").toArray();
    const QJsonArray columns = configRoot.value("columns").toArray();

    for (const QJsonValue &value : filters) {
        const QJsonObject obj = value.toObject();
        FilterDef def;
        def.id = obj.value("id").toString();
        def.type = obj.value("type").toString("combo");
        def.label = obj.value("label").toString(def.id);
        def.defaultValue = obj.value("default").toString("Any");
        const QJsonArray options = obj.value("options").toArray();
        for (const QJsonValue &opt : options) {
            def.options.append(opt.toString());
        }
        filterDefs.append(def);
    }

    for (const QJsonValue &value : columns) {
        const QJsonObject obj = value.toObject();
        ColumnDef def;
        def.key = obj.value("key").toString();
        def.title = obj.value("title").toString(def.key);
        def.width = obj.value("width").toInt(140);
        columnDefs.append(def);
    }
}

void MainWindow::seedSampleData()
{
    allRecords.clear();
}

void MainWindow::loadAudioFiles()
{
    if (transcriptionWatcher.isRunning()) {
        QMessageBox::information(this, QStringLiteral("Transcription in progress"),
                                 QStringLiteral("Please wait for current transcription queue to finish."));
        return;
    }

    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("Select Audio Files"),
        QString(),
        QStringLiteral("Audio Files (*.wav *.mp3 *.m4a *.flac *.ogg)")
    );
    if (files.isEmpty()) {
        return;
    }

    pendingTranscriptionFiles = files;
    transcriptionTotal = files.size();
    transcriptionProcessed = 0;
    transcriptionFailed = 0;
    transcriptionStatus->setText(QStringLiteral("Queued %1 file(s)").arg(transcriptionTotal));
    transcriptionProgress->setVisible(true);
    transcriptionProgress->setRange(0, transcriptionTotal);
    transcriptionProgress->setValue(0);
    processNextTranscription();
}

void MainWindow::processNextTranscription()
{
    if (pendingTranscriptionFiles.isEmpty()) {
        transcriptionStatus->setText(QStringLiteral("Transcription complete (%1/%2)")
                                     .arg(transcriptionTotal - transcriptionFailed)
                                     .arg(transcriptionTotal));
        transcriptionProgress->setValue(transcriptionTotal);
        refreshResults();
        if (transcriptionFailed > 0) {
            QMessageBox::warning(this,
                                 QStringLiteral("Some transcriptions failed"),
                                 QStringLiteral("%1 of %2 files failed to transcribe.")
                                     .arg(transcriptionFailed)
                                     .arg(transcriptionTotal));
        }
        return;
    }

    const QString path = pendingTranscriptionFiles.takeFirst();
    const QFileInfo info(path);
    transcriptionStatus->setText(QStringLiteral("Transcribing %1 (%2/%3)")
                                 .arg(info.fileName())
                                 .arg(transcriptionProcessed + 1)
                                 .arg(transcriptionTotal));

    const int id = allRecords.size() + pendingTranscriptionFiles.size() + 1;
    transcriptionWatcher.setFuture(QtConcurrent::run([path, id]() {
        Recording record;
        const QFileInfo info(path);
        record.id = id;
        record.agent = info.baseName();
        record.phone = QStringLiteral("N/A");
        record.filePath = info.absoluteFilePath();
        record.timestamp = info.lastModified();

        SearchEngine engine;
        const QString jsonPath = engine.transcriptJsonPathForAudio(record.filePath);
        SearchEngine::TranscriptData data;
        QString error;

        auto loadTxtFallback = [&record]() -> bool {
            const QFileInfo audioInfo(record.filePath);
            const QString txtPath = audioInfo.absolutePath() + "/" + audioInfo.completeBaseName() + ".txt";
            QFile textFile(txtPath);
            if (!textFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                return false;
            }
            QTextStream stream(&textFile);
            record.transcript = stream.readAll().trimmed();
            return !record.transcript.isEmpty();
        };

        if (QFileInfo::exists(jsonPath)) {
            if (!engine.loadTranscriptJson(jsonPath, &data, &error)) {
                if (!loadTxtFallback()) {
                    record.transcript = error;
                }
                return record;
            }
        } else {
            if (!engine.transcribeToJson(record.filePath, jsonPath, &error)) {
                if (!loadTxtFallback()) {
                    record.transcript = error;
                }
                return record;
            }
            if (!engine.loadTranscriptJson(jsonPath, &data, &error)) {
                if (!loadTxtFallback()) {
                    record.transcript = error;
                }
                return record;
            }
        }

        record.transcript = data.text;
        record.durationSec = data.durationSec > 0.0 ? static_cast<int>(data.durationSec) : 0;
        for (const SearchEngine::TimedWord &word : data.words) {
            Recording::TimedWord w;
            w.text = word.text;
            w.startSec = word.startSec;
            w.endSec = word.endSec;
            record.words.append(w);
        }
        return record;
    }));
}

void MainWindow::onTranscriptionFinished()
{
    const Recording record = transcriptionWatcher.result();
    ++transcriptionProcessed;
    if (record.transcript.isEmpty()) {
        ++transcriptionFailed;
    }
    allRecords.append(record);
    transcriptionProgress->setValue(transcriptionProcessed);
    processNextTranscription();
}

void MainWindow::buildDynamicFilters(QWidget *host)
{
    auto *layout = new QGridLayout();
    layout->setHorizontalSpacing(10);
    layout->setVerticalSpacing(10);

    int row = 0;
    for (const FilterDef &def : filterDefs) {
        auto *label = new QLabel(def.label, host);
        QWidget *input = nullptr;

        if (def.type == "checkbox") {
            auto *check = new QCheckBox(host);
            check->setChecked(def.defaultValue == "true");
            input = check;
            connect(check, &QCheckBox::toggled, this, &MainWindow::refreshResults);
        } else {
            auto *combo = new QComboBox(host);
            combo->addItems(def.options);
            const int index = combo->findText(def.defaultValue);
            if (index >= 0) {
                combo->setCurrentIndex(index);
            }
            input = combo;
            connect(combo, &QComboBox::currentTextChanged, this, &MainWindow::refreshResults);
        }

        filterWidgets.insert(def.id, input);
        layout->addWidget(label, row, 0);
        layout->addWidget(input, row, 1);
        ++row;
    }

    if (auto *containerLayout = qobject_cast<QVBoxLayout *>(host->layout())) {
        containerLayout->addLayout(layout);
    }
}

void MainWindow::buildDynamicTableColumns()
{
    tableModel->clear();
    QStringList headers;
    for (const ColumnDef &column : columnDefs) {
        headers << column.title;
    }
    tableModel->setHorizontalHeaderLabels(headers);

    for (int i = 0; i < columnDefs.size(); ++i) {
        resultsTable->setColumnWidth(i, columnDefs.at(i).width);
    }
}

void MainWindow::refreshResults()
{
    if (!tableModel) {
        return;
    }

    visibleRecordIndexes.clear();
    tableModel->removeRows(0, tableModel->rowCount());
    const QList<QString> keywords = parseKeywords(searchInput ? searchInput->text() : QString());

    for (int i = 0; i < allRecords.size(); ++i) {
        const Recording &record = allRecords.at(i);
        if (!matchesFilters(record, keywords)) {
            continue;
        }

        QList<QStandardItem*> rowItems;
        for (const ColumnDef &column : columnDefs) {
            auto *item = new QStandardItem(valueForColumn(record, column.key));
            item->setEditable(false);
            rowItems.append(item);
        }
        tableModel->appendRow(rowItems);
        visibleRecordIndexes.append(i);
    }

    resultsInfo->setText(QString("%1 results").arg(tableModel->rowCount()));
    if (tableModel->rowCount() > 0) {
        resultsTable->selectRow(0);
        updateDetailsPanel(0);
    } else {
        selectedVisibleRow = -1;
        currentHits.clear();
        hitsList->clear();
        metaInfo->setText("No recordings match the current search and filters.");
        playButton->setEnabled(false);
        openButton->setEnabled(false);
        waveformView->clearData();
    }
}

void MainWindow::updateDetailsPanel(int row)
{
    if (row < 0 || row >= visibleRecordIndexes.size()) {
        return;
    }
    selectedVisibleRow = row;

    const Recording &record = allRecords.at(visibleRecordIndexes.at(row));
    const QList<QString> keywords = parseKeywords(searchInput->text());

    metaInfo->setText(
        QString("File: %1\nAgent: %2\nPhone: %3\nDuration: %4 sec\nTimestamp: %5")
            .arg(record.filePath, record.agent, record.phone)
            .arg(record.durationSec)
            .arg(record.timestamp.toString(Qt::ISODate))
    );
    waveformView->loadAudio(record.filePath);

    hitsList->clear();
    currentHits = detectKeywordHits(record, keywords);
    if (!currentHits.isEmpty()) {
        for (const KeywordHit &hit : currentHits) {
            hitsList->addItem(QString("%1 - \"%2\" (%3 to %4)")
                              .arg(formatSec(hit.startSec), hit.keyword, formatSec(hit.startSec), formatSec(hit.endSec)));
        }
        hitsList->setCurrentRow(0);
    } else {
        hitsList->addItem("No keyword hit in transcript preview.");
        hitsList->addItem("Tip: add sidecar transcript .txt files for better hit accuracy.");
    }

    playButton->setEnabled(!currentHits.isEmpty());
    openButton->setEnabled(true);
}

void MainWindow::playSelectedSnippet()
{
    if (selectedVisibleRow < 0 || selectedVisibleRow >= visibleRecordIndexes.size()) {
        return;
    }
    const int hitRow = hitsList->currentRow();
    if (hitRow < 0 || hitRow >= currentHits.size()) {
        return;
    }
    const Recording &record = allRecords.at(visibleRecordIndexes.at(selectedVisibleRow));
    const KeywordHit &hit = currentHits.at(hitRow);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    player->setSource(QUrl::fromLocalFile(record.filePath));
#else
    player->setMedia(QUrl::fromLocalFile(record.filePath));
#endif
    player->setPosition(static_cast<qint64>(hit.startSec * 1000.0));
    player->play();
    snippetStopTimer->start(static_cast<int>(qMax(300.0, (hit.endSec - hit.startSec) * 1000.0)));
}

void MainWindow::openFullAudio()
{
    if (selectedVisibleRow < 0 || selectedVisibleRow >= visibleRecordIndexes.size()) {
        return;
    }
    const Recording &record = allRecords.at(visibleRecordIndexes.at(selectedVisibleRow));
    QDesktopServices::openUrl(QUrl::fromLocalFile(record.filePath));
}

void MainWindow::stopSnippetPlayback()
{
    if (player) {
        player->pause();
    }
}

QString MainWindow::valueForColumn(const Recording &record, const QString &key) const
{
    if (key == "id") {
        return QString::number(record.id);
    }
    if (key == "agent") {
        return record.agent;
    }
    if (key == "phone") {
        return record.phone;
    }
    if (key == "timestamp") {
        return record.timestamp.toString("yyyy-MM-dd HH:mm");
    }
    if (key == "duration") {
        return QString("%1 sec").arg(record.durationSec);
    }
    if (key == "transcript") {
        return record.transcript;
    }
    return {};
}

bool MainWindow::matchesFilters(const Recording &record, const QList<QString> &keywords) const
{
    if (!keywords.isEmpty()) {
        const QList<KeywordHit> hits = detectKeywordHits(record, keywords);
        if (hits.isEmpty()) {
            return false;
        }
    }

    const auto minDurationWidget = qobject_cast<QComboBox *>(filterWidgets.value("min_duration_sec"));
    if (minDurationWidget) {
        bool ok = false;
        const int minDuration = minDurationWidget->currentText().toInt(&ok);
        if (ok && record.durationSec < minDuration) {
            return false;
        }
    }

    const auto highRiskWidget = qobject_cast<QCheckBox *>(filterWidgets.value("high_risk_only"));
    if (highRiskWidget && highRiskWidget->isChecked()) {
        const QString text = normalizeWord(record.transcript);
        const bool highRisk = text.contains("fraud") || text.contains("legal") || text.contains("dispute");
        if (!highRisk) {
            return false;
        }
    }

    return true;
}

QString MainWindow::normalizeWord(const QString &value) const
{
    return value.trimmed().toLower();
}

QString MainWindow::loadTranscriptForAudio(const QString &audioPath) const
{
    const QFileInfo audioInfo(audioPath);
    const QString txtPath = audioInfo.absolutePath() + "/" + audioInfo.completeBaseName() + ".txt";
    QFile textFile(txtPath);
    if (!textFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QTextStream stream(&textFile);
    return stream.readAll().trimmed();
}

int MainWindow::estimateDurationSecFromWav(const QString &audioPath) const
{
    QFile file(audioPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    if (file.size() < 44) {
        return 0;
    }
    const QByteArray header = file.read(44);
    if (header.size() < 44 || header.mid(0, 4) != "RIFF" || header.mid(8, 4) != "WAVE") {
        return 0;
    }
    const quint32 sampleRate = static_cast<quint32>(static_cast<unsigned char>(header[24]) |
                                                     (static_cast<unsigned char>(header[25]) << 8) |
                                                     (static_cast<unsigned char>(header[26]) << 16) |
                                                     (static_cast<unsigned char>(header[27]) << 24));
    const quint16 channels = static_cast<quint16>(static_cast<unsigned char>(header[22]) |
                                                   (static_cast<unsigned char>(header[23]) << 8));
    const quint16 bits = static_cast<quint16>(static_cast<unsigned char>(header[34]) |
                                               (static_cast<unsigned char>(header[35]) << 8));
    if (sampleRate == 0 || channels == 0 || bits == 0) {
        return 0;
    }
    const quint32 byteRate = sampleRate * channels * (bits / 8);
    if (byteRate == 0) {
        return 0;
    }
    return static_cast<int>(file.size() / byteRate);
}

QList<QString> MainWindow::parseKeywords(const QString &value) const
{
    QList<QString> keywords;
    const QStringList parts = value.split(QRegularExpression("[,;\\n]+"), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString normalized = normalizeWord(part);
        if (!normalized.isEmpty()) {
            keywords.append(normalized);
        }
    }
    return keywords;
}

QList<MainWindow::KeywordHit> MainWindow::detectKeywordHits(const Recording &record, const QList<QString> &keywords) const
{
    QList<KeywordHit> hits;
    if (keywords.isEmpty()) {
        return hits;
    }

    for (const QString &keyword : keywords) {
        const QStringList phrase = keyword.split(' ', Qt::SkipEmptyParts);
        if (phrase.isEmpty()) {
            continue;
        }
        const bool isPhrase = phrase.size() > 1;

        if (!record.words.isEmpty()) {
            if (isPhrase) {
                for (int i = 0; i + phrase.size() <= record.words.size(); ++i) {
                    bool match = true;
                    for (int j = 0; j < phrase.size(); ++j) {
                        if (normalizeToken(record.words.at(i + j).text) != phrase.at(j)) {
                            match = false;
                            break;
                        }
                    }
                    if (!match) {
                        continue;
                    }
                    KeywordHit hit;
                    hit.keyword = keyword;
                    hit.startSec = qMax(0.0, record.words.at(i).startSec - 0.8);
                    hit.endSec = qMax(hit.startSec + 0.4, record.words.at(i + phrase.size() - 1).endSec + 1.6);
                    hits.append(hit);
                }
            } else {
                for (const Recording::TimedWord &word : record.words) {
                    if (normalizeToken(word.text) != phrase.first()) {
                        continue;
                    }
                    KeywordHit hit;
                    hit.keyword = keyword;
                    hit.startSec = qMax(0.0, word.startSec - 0.8);
                    hit.endSec = qMax(hit.startSec + 0.4, word.endSec + 1.6);
                    hits.append(hit);
                }
            }
            continue;
        }

        // Fallback when no word-level timestamps are available.
        const QString transcriptNorm = normalizeWord(record.transcript);
        const int transcriptLen = qMax(1, transcriptNorm.size());
        const double duration = qMax(5, record.durationSec);
        int from = 0;
        while (from < transcriptLen) {
            const int index = transcriptNorm.indexOf(keyword, from);
            if (index < 0) {
                break;
            }
            const double centerSec = (static_cast<double>(index) / transcriptLen) * duration;
            KeywordHit hit;
            hit.keyword = keyword;
            hit.startSec = qMax(0.0, centerSec - 2.0);
            hit.endSec = qMin(duration, centerSec + 3.0);
            hits.append(hit);
            from = index + keyword.size();
        }
    }
    return hits;
}

QString MainWindow::formatSec(double sec)
{
    const int total = qMax(0, static_cast<int>(sec));
    const int min = total / 60;
    const int rem = total % 60;
    return QString("%1:%2")
        .arg(min, 2, 10, QLatin1Char('0'))
        .arg(rem, 2, 10, QLatin1Char('0'));
}

bool MainWindow::ensureTranscriptForRecord(Recording &record)
{
    const QString jsonPath = searchEngine.transcriptJsonPathForAudio(record.filePath);
    if (QFileInfo::exists(jsonPath)) {
        SearchEngine::TranscriptData data;
        QString error;
        if (!searchEngine.loadTranscriptJson(jsonPath, &data, &error)) {
            record.transcript = error;
            return false;
        }
        record.transcript = data.text;
        if (data.durationSec > 0.0) {
            record.durationSec = static_cast<int>(data.durationSec);
        }
        record.words.clear();
        for (const SearchEngine::TimedWord &word : data.words) {
            Recording::TimedWord w;
            w.text = word.text;
            w.startSec = word.startSec;
            w.endSec = word.endSec;
            record.words.append(w);
        }
        return true;
    }

    QString errorMessage;
    if (!searchEngine.transcribeToJson(record.filePath, jsonPath, &errorMessage)) {
        const QString txtFallback = loadTranscriptForAudio(record.filePath);
        if (!txtFallback.isEmpty()) {
            record.transcript = txtFallback;
            return true;
        }
        record.transcript = errorMessage;
        return false;
    }
    SearchEngine::TranscriptData data;
    if (!searchEngine.loadTranscriptJson(jsonPath, &data, &errorMessage)) {
        record.transcript = errorMessage;
        return false;
    }
    record.transcript = data.text;
    if (data.durationSec > 0.0) {
        record.durationSec = static_cast<int>(data.durationSec);
    }
    record.words.clear();
    for (const SearchEngine::TimedWord &word : data.words) {
        Recording::TimedWord w;
        w.text = word.text;
        w.startSec = word.startSec;
        w.endSec = word.endSec;
        record.words.append(w);
    }
    return true;
}

QString MainWindow::normalizeToken(const QString &value) const
{
    QString token = normalizeWord(value);
    token.remove(QRegularExpression("^[^a-z0-9]+|[^a-z0-9]+$"));
    return token;
}
