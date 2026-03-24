#include "mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
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
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    loadConfig();
    seedSampleData();
    setupWindow();
    setupLayout();
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
    searchInput->setPlaceholderText("Search keyword (example: dispute, refund, contract)...");
    searchInput->setClearButtonEnabled(true);
    searchInput->setMinimumHeight(40);

    auto *searchButton = new QPushButton("Search", root);
    searchButton->setMinimumHeight(40);

    resultsInfo = new QLabel("0 results", root);
    resultsInfo->setObjectName("MutedLabel");

    searchRow->addWidget(searchInput, 1);
    searchRow->addWidget(searchButton);
    searchRow->addSpacing(8);
    searchRow->addWidget(resultsInfo);

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

    auto *actionRow = new QHBoxLayout();
    playButton = new QPushButton("Play Snippet", reviewGroup);
    openButton = new QPushButton("Open Full Audio", reviewGroup);
    playButton->setEnabled(false);
    openButton->setEnabled(false);
    actionRow->addWidget(playButton);
    actionRow->addWidget(openButton);

    reviewLayout->addWidget(metaInfo);
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
    connect(searchInput, &QLineEdit::returnPressed, this, &MainWindow::refreshResults);
    connect(resultsTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &current, const QModelIndex &) {
        updateDetailsPanel(current.row());
    });
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
    allRecords = {
        {101, "Alice Doe", "+1-202-555-0111", "Customer requests refund due to billing dispute in March invoice.", "records/call_101.wav", QDateTime::currentDateTime().addDays(-2), 342, "English"},
        {102, "Ravi Singh", "+1-202-555-0145", "Client asks to renew annual contract and confirms discount offer.", "records/call_102.wav", QDateTime::currentDateTime().addDays(-1), 611, "English"},
        {103, "Maria Chen", "+1-202-555-0188", "Caller reports fraud alert and wants immediate card block.", "records/call_103.wav", QDateTime::currentDateTime().addSecs(-7400), 278, "English"},
        {104, "Samir Patel", "+1-202-555-0164", "Conversation in Spanish about payment plan and due date extension.", "records/call_104.wav", QDateTime::currentDateTime().addDays(-4), 505, "Spanish"},
        {105, "Nina Park", "+1-202-555-0190", "Escalation request mentions legal complaint and disputed service charge.", "records/call_105.wav", QDateTime::currentDateTime().addDays(-6), 722, "English"}
    };
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
    const QString query = normalizeWord(searchInput ? searchInput->text() : QString());

    for (int i = 0; i < allRecords.size(); ++i) {
        const Recording &record = allRecords.at(i);
        if (!matchesFilters(record, query)) {
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
        hitsList->clear();
        metaInfo->setText("No recordings match the current search and filters.");
        playButton->setEnabled(false);
        openButton->setEnabled(false);
    }
}

void MainWindow::updateDetailsPanel(int row)
{
    if (row < 0 || row >= visibleRecordIndexes.size()) {
        return;
    }

    const Recording &record = allRecords.at(visibleRecordIndexes.at(row));
    const QString query = normalizeWord(searchInput->text());

    metaInfo->setText(
        QString("File: %1\nAgent: %2\nPhone: %3\nLanguage: %4\nDuration: %5 sec\nTimestamp: %6")
            .arg(record.filePath, record.agent, record.phone, record.language)
            .arg(record.durationSec)
            .arg(record.timestamp.toString(Qt::ISODate))
    );

    hitsList->clear();
    const QString transcriptNorm = normalizeWord(record.transcript);
    if (!query.isEmpty() && transcriptNorm.contains(query)) {
        hitsList->addItem(QString("Potential hit at 00:12 - \"%1\"").arg(query));
        hitsList->addItem(QString("Potential hit at 01:07 - \"%1\"").arg(query));
    } else {
        hitsList->addItem("No exact keyword hit in transcript preview.");
        hitsList->addItem("Tip: widen search term or use language filter.");
    }

    playButton->setEnabled(true);
    openButton->setEnabled(true);
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
    if (key == "language") {
        return record.language;
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

bool MainWindow::matchesFilters(const Recording &record, const QString &query) const
{
    if (!query.isEmpty() && !normalizeWord(record.transcript).contains(query)) {
        return false;
    }

    const auto languageWidget = qobject_cast<QComboBox *>(filterWidgets.value("language"));
    if (languageWidget && languageWidget->currentText() != "Any" &&
        record.language != languageWidget->currentText()) {
        return false;
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
